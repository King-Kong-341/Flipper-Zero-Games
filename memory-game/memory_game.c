/*
 * Memory Game for the Flipper Zero
 * ---------------------------------
 * A Simon-Says style memory game. Watch the arrow sequence light up, then
 * repeat it. Every round the sequence grows by one more step.
 *
 * Controls:
 *   Title screen:  Up/Down = select Start/Settings, OK = confirm,
 *                  Left = Rules, Right = Leaderboard
 *   Watching:      just watch - LED blinks blue for every step
 *   Your turn:     press the arrow key that lit up - no OK needed.
 *                  LED blinks green when correct, red when wrong.
 *   Back (short):  in-game -> "Quit game?" prompt, elsewhere -> go back
 *   Back (1s):     instantly ends the current run
 *   Game over:     Left = Exit app, OK = New game, Back = Menu, Right = List
 *
 * The top 5 all-time results are saved automatically to the SD card and
 * re-sorted after every run - no name entry needed. Sound / vibration /
 * LED feedback can each be toggled independently in Settings.
 */

#include <furi.h>
#include <furi_hal_random.h>
#include <furi_hal_speaker.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TAG "MemoryGame"

/* ---------- Constants ---------- */

#define TICK_MS 30
#define FLASH_ON_MS 450
#define FLASH_OFF_MS 220
#define ROUND_INTRO_MS 700
#define RESULT_PAUSE_MS 900
#define WRONG_FLASH_MS 450
#define CORRECT_FLASH_MS 180

#define MAX_PATTERN 64
#define LEADERBOARD_SIZE 5
#define LEADERBOARD_VISIBLE 3

#define SCORES_PATH APP_DATA_PATH("scores.txt")
#define SETTINGS_PATH APP_DATA_PATH("settings.txt")

static const char* const rules_lines[] = {
    "Watch the arrows light",
    "up on screen in order.",
    "",
    "Then repeat: press the",
    "matching arrow key.",
    "No OK needed - each",
    "press IS your guess.",
    "",
    "Every round the pattern",
    "grows by one more step.",
    "",
    "Hold Back for 1s to quit",
    "instantly during a run.",
};
#define RULES_LINE_COUNT (sizeof(rules_lines) / sizeof(rules_lines[0]))
#define RULES_VISIBLE 4

/* Settings rows, in display order: */
#define SETTINGS_SOUND 0
#define SETTINGS_VOLUME 1
#define SETTINGS_VIBRO 2
#define SETTINGS_LED 3
#define SETTINGS_RESET 4
#define SETTINGS_ITEM_COUNT 5
#define SETTINGS_VISIBLE 2

typedef enum {
    DirUp = 0,
    DirRight = 1,
    DirDown = 2,
    DirLeft = 3,
} Direction;

typedef enum {
    ScreenMenu,
    ScreenSettings,
    ScreenRules,
    ScreenLeaderboard,
    ScreenRoundIntro,
    ScreenWatching,
    ScreenInput,
    ScreenRoundSuccess,
    ScreenConfirmExit,
    ScreenGameOver,
} Screen;

typedef enum {
    SubFlashOn,
    SubFlashOff,
} WatchSub;

typedef enum {
    EventTypeKey,
    EventTypeTick,
} EventType;

typedef struct {
    EventType type;
    InputEvent input;
} GameEvent;

typedef struct {
    uint8_t level;
    uint8_t count; /* how many times this level was reached (shown as "2x") */
} ScoreEntry;

typedef struct {
    Screen screen;
    Screen return_screen;
    bool running;
    uint32_t anim_tick;

    /* Game data */
    uint8_t pattern[MAX_PATTERN];
    uint8_t level; /* length of the current sequence / current round */
    uint8_t watch_step;
    WatchSub watch_sub;
    int32_t sub_timer_ms;
    uint8_t input_index;
    uint8_t last_input_dir;
    bool input_was_wrong;
    bool flash_input_feedback;
    int32_t feedback_timer_ms;
    uint8_t final_level;
    bool was_new_record;

    /* Quit confirmation */
    bool confirm_yes;

    /* Title screen menu (Start / Settings) */
    uint8_t menu_selection;

    /* Rules screen scroll position */
    uint8_t rules_scroll;

    /* Leaderboard scroll position */
    uint8_t leaderboard_scroll;

    /* Settings */
    uint8_t settings_selection;
    uint8_t settings_scroll;
    bool sound_enabled;
    uint8_t volume; /* 0-100, in steps of 10 */
    bool vibro_enabled;
    bool led_enabled;
    int32_t toast_timer_ms;

    /* Leaderboard */
    ScoreEntry scores[LEADERBOARD_SIZE];
    uint8_t scores_count;

    /* System */
    FuriMessageQueue* queue;
    FuriTimer* timer;
    NotificationApp* notification;
    Storage* storage;
    bool speaker_owned;
    bool led_held; /* blue LED currently kept on during playback */
} GameApp;

/* ---------- Sound & LED ---------- */

static void tone_start(GameApp* app, float freq) {
    if(!app->sound_enabled || app->volume == 0) return;
    if(!app->speaker_owned) {
        if(furi_hal_speaker_acquire(30)) {
            app->speaker_owned = true;
        }
    }
    if(app->speaker_owned) {
        float gain = ((float)app->volume / 100.0f) * 0.7f;
        furi_hal_speaker_start(freq, gain);
    }
}

static void tone_stop(GameApp* app) {
    if(app->led_held) {
        notification_message(app->notification, &sequence_reset_rgb);
        app->led_held = false;
    }
    if(app->speaker_owned) {
        furi_hal_speaker_stop();
        furi_hal_speaker_release();
        app->speaker_owned = false;
    }
}

static void tone_blip(GameApp* app, float freq, uint32_t ms) {
    if(!app->sound_enabled || app->volume == 0) return;
    tone_start(app, freq);
    furi_delay_ms(ms);
    tone_stop(app);
}

static float dir_freq(Direction d) {
    switch(d) {
    case DirUp:
        return 523.25f; /* C5 */
    case DirRight:
        return 659.25f; /* E5 */
    case DirDown:
        return 392.00f; /* G4 */
    case DirLeft:
        return 293.66f; /* D4 */
    default:
        return 440.0f;
    }
}

static void fx_led(GameApp* app, const NotificationSequence* seq) {
    if(app->led_enabled) notification_message(app->notification, seq);
}

static void fx_vibro(GameApp* app) {
    if(app->vibro_enabled) notification_message(app->notification, &sequence_single_vibro);
}

/* Shown while the sequence plays back - blue LED stays on for as long as the
 * field is lit / the tone plays; tone_stop() switches it off again. */
static void play_watch_flash(GameApp* app, Direction d) {
    if(app->led_enabled) {
        notification_message(app->notification, &sequence_set_only_blue_255);
        app->led_held = true;
    }
    tone_start(app, dir_freq(d));
}

/* Player guessed one step correctly - green LED + tone, but no vibration:
 * vibrating on every single key press gets annoying, so the buzz is kept
 * for a fully completed round (see play_round_success_fx). */
static void play_correct_guess(GameApp* app, Direction d) {
    fx_led(app, &sequence_blink_green_100);
    tone_start(app, dir_freq(d));
}

/* Retro "you lost" jingle: a descending run of notes. */
static void play_fail_jingle(GameApp* app) {
    static const float notes[] = {523.25f, 440.0f, 349.23f, 293.66f, 220.0f, 164.81f};
    for(uint8_t i = 0; i < sizeof(notes) / sizeof(notes[0]); i++) {
        tone_blip(app, notes[i], 110);
    }
}

/* Player guessed wrong - red LED, a buzz and the descending jingle. */
static void play_wrong_guess(GameApp* app) {
    /* red LED stays on for the whole jingle (6 notes x 110ms) */
    if(app->led_enabled) {
        notification_message(app->notification, &sequence_set_only_red_255);
    }
    fx_vibro(app);
    if(app->sound_enabled) {
        play_fail_jingle(app);
    } else {
        furi_delay_ms(660);
    }
    if(app->led_enabled) {
        notification_message(app->notification, &sequence_reset_rgb);
    }
}

/* Whole round cleared successfully. */
static void play_round_success_fx(GameApp* app) {
    fx_led(app, &sequence_blink_green_100);
    fx_vibro(app);
    tone_blip(app, 784.0f, 90);
    tone_blip(app, 1046.5f, 140);
}

/* Player quit mid-run: just two short falling notes, not the full jingle. */
static void play_abort_fx(GameApp* app) {
    fx_led(app, &sequence_blink_red_100);
    fx_vibro(app);
    tone_blip(app, 440.0f, 120);
    tone_blip(app, 293.66f, 160);
}

/* ---------- Leaderboard ---------- */

static void load_scores(GameApp* app) {
    app->scores_count = 0;
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, SCORES_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[128];
        size_t read = storage_file_read(file, buf, sizeof(buf) - 1);
        buf[read] = '\0';

        char* pos = buf;
        while(*pos != '\0' && app->scores_count < LEADERBOARD_SIZE) {
            char* line_end = strchr(pos, '\n');
            if(line_end) *line_end = '\0';

            if(*pos != '\0') {
                /* line format: "level,count" (older files: just "level") */
                app->scores[app->scores_count].level = (uint8_t)atoi(pos);
                char* comma = strchr(pos, ',');
                int cnt = comma ? atoi(comma + 1) : 1;
                app->scores[app->scores_count].count = (uint8_t)(cnt < 1 ? 1 : cnt);
                app->scores_count++;
            }

            if(!line_end) break;
            pos = line_end + 1;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void save_scores(GameApp* app) {
    storage_common_mkdir(app->storage, APP_DATA_PATH(""));
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, SCORES_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char line[16];
        for(uint8_t i = 0; i < app->scores_count; i++) {
            int len = snprintf(
                line, sizeof(line), "%u,%u\n", app->scores[i].level, app->scores[i].count);
            storage_file_write(file, line, len);
        }
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void reset_scores(GameApp* app) {
    app->scores_count = 0;
    app->leaderboard_scroll = 0;
    save_scores(app);
}

static bool is_new_record(GameApp* app, uint8_t level) {
    if(level == 0) return false;
    if(app->scores_count < LEADERBOARD_SIZE) return true;
    return level > app->scores[LEADERBOARD_SIZE - 1].level;
}

/* Inserts a level into the sorted top-5 list (highest first) and saves it. */
static void insert_score(GameApp* app, uint8_t level) {
    uint8_t pos = app->scores_count;
    for(uint8_t i = 0; i < app->scores_count; i++) {
        if(level > app->scores[i].level) {
            pos = i;
            break;
        }
    }
    uint8_t count = app->scores_count < LEADERBOARD_SIZE ? app->scores_count + 1 :
                                                            LEADERBOARD_SIZE;
    for(int i = (int)count - 1; i > (int)pos; i--) {
        app->scores[i] = app->scores[i - 1];
    }
    app->scores[pos].level = level;
    app->scores[pos].count = 1;
    app->scores_count = count;
    save_scores(app);
}

/* If this level is already on the list, count it once more ("2x") instead of
 * taking a new place. Returns true when it was handled that way. */
static bool bump_existing_score(GameApp* app, uint8_t level) {
    for(uint8_t i = 0; i < app->scores_count; i++) {
        if(app->scores[i].level == level) {
            if(app->scores[i].count < 99) app->scores[i].count++;
            save_scores(app);
            return true;
        }
    }
    return false;
}

/* ---------- Settings ---------- */

static void load_settings(GameApp* app) {
    app->sound_enabled = true;
    app->volume = 100;
    app->vibro_enabled = true;
    app->led_enabled = true;

    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, SETTINGS_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        char buf[16];
        size_t read = storage_file_read(file, buf, sizeof(buf) - 1);
        buf[read] = '\0';
        if(read >= 3) {
            app->sound_enabled = buf[0] == '1';
            app->vibro_enabled = buf[1] == '1';
            app->led_enabled = buf[2] == '1';
        }
        /* New format appends ",<volume>" after the 3 flags; older saved
         * files without it keep the default volume of 100. */
        char* comma = strchr(buf, ',');
        if(comma) {
            int vol = atoi(comma + 1);
            if(vol < 0) vol = 0;
            if(vol > 100) vol = 100;
            app->volume = (uint8_t)vol;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
}

static void save_settings(GameApp* app) {
    storage_common_mkdir(app->storage, APP_DATA_PATH(""));
    File* file = storage_file_alloc(app->storage);
    if(storage_file_open(file, SETTINGS_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        char buf[16];
        int len = snprintf(
            buf,
            sizeof(buf),
            "%c%c%c,%u\n",
            app->sound_enabled ? '1' : '0',
            app->vibro_enabled ? '1' : '0',
            app->led_enabled ? '1' : '0',
            app->volume);
        storage_file_write(file, buf, len);
    }
    storage_file_close(file);
    storage_file_free(file);
}

/* ---------- Game flow ---------- */

static void start_watch_round(GameApp* app) {
    app->watch_step = 0;
    app->watch_sub = SubFlashOn;
    app->sub_timer_ms = FLASH_ON_MS;
    app->input_index = 0;
    app->flash_input_feedback = false;
    play_watch_flash(app, (Direction)app->pattern[0]);
    app->screen = ScreenWatching;
}

static void start_new_game(GameApp* app) {
    app->level = 1;
    app->pattern[0] = (uint8_t)(furi_hal_random_get() % 4);
    app->sub_timer_ms = ROUND_INTRO_MS;
    app->screen = ScreenRoundIntro;
}

static void go_to_menu(GameApp* app) {
    tone_stop(app);
    app->screen = ScreenMenu;
}

/* Ends the run, automatically saves it into the top-5 if it qualifies. */
static void finish_game(GameApp* app) {
    tone_stop(app);
    app->final_level = app->level > 0 ? app->level - 1 : 0;
    app->was_new_record = false;
    if(app->final_level > 0 && bump_existing_score(app, app->final_level)) {
        /* same level again: just raises its "Nx" counter, not a new place */
    } else if(is_new_record(app, app->final_level)) {
        app->was_new_record = true;
        insert_score(app, app->final_level);
    }
    app->screen = ScreenGameOver;
}

/* Called when the player bails out mid-run (long Back, or Confirm-Yes). */
static void abort_game(GameApp* app) {
    play_abort_fx(app);
    finish_game(app);
}

/* ---------- Tick handling ---------- */

static void handle_tick(GameApp* app) {
    app->anim_tick++;

    switch(app->screen) {
    case ScreenRoundIntro:
        app->sub_timer_ms -= TICK_MS;
        if(app->sub_timer_ms <= 0) {
            start_watch_round(app);
        }
        break;

    case ScreenWatching:
        app->sub_timer_ms -= TICK_MS;
        if(app->sub_timer_ms <= 0) {
            if(app->watch_sub == SubFlashOn) {
                tone_stop(app);
                app->watch_sub = SubFlashOff;
                app->sub_timer_ms = FLASH_OFF_MS;
            } else {
                app->watch_step++;
                if(app->watch_step >= app->level) {
                    app->screen = ScreenInput;
                } else {
                    app->watch_sub = SubFlashOn;
                    app->sub_timer_ms = FLASH_ON_MS;
                    play_watch_flash(app, (Direction)app->pattern[app->watch_step]);
                }
            }
        }
        break;

    case ScreenInput:
        if(app->flash_input_feedback) {
            app->feedback_timer_ms -= TICK_MS;
            if(app->feedback_timer_ms <= 0) {
                app->flash_input_feedback = false;
                tone_stop(app);
                if(app->input_was_wrong) {
                    finish_game(app);
                }
            }
        }
        break;

    case ScreenRoundSuccess:
        app->sub_timer_ms -= TICK_MS;
        if(app->sub_timer_ms <= 0) {
            /* prepare next round */
            app->pattern[app->level] = (uint8_t)(furi_hal_random_get() % 4);
            app->level++;
            app->sub_timer_ms = ROUND_INTRO_MS;
            app->screen = ScreenRoundIntro;
        }
        break;

    case ScreenSettings:
        if(app->toast_timer_ms > 0) {
            app->toast_timer_ms -= TICK_MS;
            if(app->toast_timer_ms < 0) app->toast_timer_ms = 0;
        }
        break;

    default:
        break;
    }
}

/* ---------- Input handling ---------- */

/* Pressing a direction key IS the guess - no OK confirmation needed. */
static void handle_input_guess(GameApp* app, Direction d) {
    if(app->flash_input_feedback) return; /* briefly ignore repeats while flashing */

    app->last_input_dir = (uint8_t)d;
    app->flash_input_feedback = true;

    if(d == app->pattern[app->input_index]) {
        app->input_was_wrong = false;
        play_correct_guess(app, d);
        app->feedback_timer_ms = CORRECT_FLASH_MS;
        app->input_index++;
        if(app->input_index >= app->level) {
            play_round_success_fx(app);
            app->sub_timer_ms = RESULT_PAUSE_MS;
            app->screen = ScreenRoundSuccess;
        }
    } else {
        app->input_was_wrong = true;
        play_wrong_guess(app);
        app->feedback_timer_ms = WRONG_FLASH_MS;
    }
}

static void handle_input(GameApp* app, InputEvent* ev) {
    if(ev->type != InputTypeShort && ev->type != InputTypeLong) return;

    /* Global panic button: Back held 1s = end the run immediately. */
    if(ev->key == InputKeyBack && ev->type == InputTypeLong) {
        if(app->screen == ScreenWatching || app->screen == ScreenInput ||
           app->screen == ScreenRoundIntro || app->screen == ScreenConfirmExit ||
           app->screen == ScreenRoundSuccess) {
            abort_game(app);
            return;
        }
        if(app->screen == ScreenMenu) {
            app->running = false;
            return;
        }
        go_to_menu(app);
        return;
    }

    if(ev->type != InputTypeShort) return;

    switch(app->screen) {
    case ScreenMenu:
        if(ev->key == InputKeyLeft) {
            app->rules_scroll = 0;
            app->return_screen = ScreenMenu;
            app->screen = ScreenRules;
        } else if(ev->key == InputKeyRight) {
            app->leaderboard_scroll = 0;
            app->return_screen = ScreenMenu;
            app->screen = ScreenLeaderboard;
        } else if(ev->key == InputKeyUp || ev->key == InputKeyDown) {
            app->menu_selection = app->menu_selection == 0 ? 1 : 0;
        } else if(ev->key == InputKeyOk) {
            if(app->menu_selection == 0) {
                start_new_game(app);
            } else {
                app->settings_selection = 0;
                app->settings_scroll = 0;
                app->toast_timer_ms = 0;
                app->screen = ScreenSettings;
            }
        } else if(ev->key == InputKeyBack) {
            app->running = false;
        }
        break;

    case ScreenSettings:
        if(ev->key == InputKeyBack) {
            app->screen = ScreenMenu;
        } else if(ev->key == InputKeyLeft) {
            if(app->settings_selection == SETTINGS_VOLUME) {
                app->volume = app->volume >= 10 ? app->volume - 10 : 0;
                save_settings(app);
            } else {
                app->screen = ScreenMenu;
            }
        } else if(ev->key == InputKeyRight) {
            if(app->settings_selection == SETTINGS_VOLUME) {
                app->volume = app->volume <= 90 ? app->volume + 10 : 100;
                save_settings(app);
            }
        } else if(ev->key == InputKeyUp) {
            /* no wrap-around: stops at the first item */
            if(app->settings_selection > 0) app->settings_selection--;
            if(app->settings_selection < app->settings_scroll) {
                app->settings_scroll = app->settings_selection;
            }
        } else if(ev->key == InputKeyDown) {
            /* no wrap-around: stops at the last item */
            if(app->settings_selection < SETTINGS_ITEM_COUNT - 1) app->settings_selection++;
            if(app->settings_selection >= app->settings_scroll + SETTINGS_VISIBLE) {
                app->settings_scroll = app->settings_selection - SETTINGS_VISIBLE + 1;
            }
        } else if(ev->key == InputKeyOk) {
            switch(app->settings_selection) {
            case SETTINGS_SOUND:
                app->sound_enabled = !app->sound_enabled;
                save_settings(app);
                break;
            case SETTINGS_VIBRO:
                app->vibro_enabled = !app->vibro_enabled;
                save_settings(app);
                break;
            case SETTINGS_LED:
                app->led_enabled = !app->led_enabled;
                save_settings(app);
                break;
            case SETTINGS_RESET:
                reset_scores(app);
                app->toast_timer_ms = 1000;
                break;
            }
        }
        break;

    case ScreenRules:
        if(ev->key == InputKeyBack || ev->key == InputKeyLeft) {
            app->screen = app->return_screen;
        } else if(ev->key == InputKeyUp) {
            if(app->rules_scroll > 0) app->rules_scroll--;
        } else if(ev->key == InputKeyDown) {
            uint8_t max_scroll =
                RULES_LINE_COUNT > RULES_VISIBLE ? RULES_LINE_COUNT - RULES_VISIBLE : 0;
            if(app->rules_scroll < max_scroll) app->rules_scroll++;
        }
        break;

    case ScreenLeaderboard:
        if(ev->key == InputKeyBack || ev->key == InputKeyLeft) {
            app->screen = app->return_screen;
        } else if(ev->key == InputKeyUp) {
            if(app->leaderboard_scroll > 0) app->leaderboard_scroll--;
        } else if(ev->key == InputKeyDown) {
            uint8_t max_scroll = app->scores_count > LEADERBOARD_VISIBLE ?
                                      app->scores_count - LEADERBOARD_VISIBLE :
                                      0;
            if(app->leaderboard_scroll < max_scroll) app->leaderboard_scroll++;
        }
        break;

    case ScreenRoundIntro:
    case ScreenRoundSuccess:
        if(ev->key == InputKeyBack) {
            app->return_screen = app->screen;
            app->confirm_yes = false;
            app->screen = ScreenConfirmExit;
        }
        break;

    case ScreenWatching:
        if(ev->key == InputKeyBack) {
            tone_stop(app);
            app->return_screen = ScreenWatching;
            app->confirm_yes = false;
            app->screen = ScreenConfirmExit;
        }
        break;

    case ScreenInput:
        if(ev->key == InputKeyBack) {
            tone_stop(app);
            app->return_screen = ScreenInput;
            app->confirm_yes = false;
            app->screen = ScreenConfirmExit;
        } else if(ev->key == InputKeyUp) {
            handle_input_guess(app, DirUp);
        } else if(ev->key == InputKeyRight) {
            handle_input_guess(app, DirRight);
        } else if(ev->key == InputKeyDown) {
            handle_input_guess(app, DirDown);
        } else if(ev->key == InputKeyLeft) {
            handle_input_guess(app, DirLeft);
        }
        break;

    case ScreenConfirmExit:
        if(ev->key == InputKeyLeft || ev->key == InputKeyRight) {
            app->confirm_yes = !app->confirm_yes;
        } else if(ev->key == InputKeyOk) {
            if(app->confirm_yes) {
                abort_game(app);
            } else {
                app->screen = app->return_screen;
            }
        } else if(ev->key == InputKeyBack) {
            app->screen = app->return_screen;
        }
        break;

    case ScreenGameOver:
        if(ev->key == InputKeyLeft) {
            go_to_menu(app);
        } else if(ev->key == InputKeyOk) {
            start_new_game(app);
        } else if(ev->key == InputKeyBack) {
            go_to_menu(app);
        } else if(ev->key == InputKeyRight) {
            app->leaderboard_scroll = 0;
            app->return_screen = ScreenGameOver;
            app->screen = ScreenLeaderboard;
        }
        break;
    }
}

/* ---------- Drawing ---------- */

typedef struct {
    int16_t x, y, w, h;
    int16_t cx, cy;
    char arrow;
} BoxDef;

static BoxDef box_for(Direction d) {
    BoxDef b = {0};
    switch(d) {
    case DirUp:
        b.x = 46;
        b.y = 2;
        b.w = 36;
        b.h = 18;
        b.arrow = '^';
        break;
    case DirDown:
        b.x = 46;
        b.y = 44;
        b.w = 36;
        b.h = 18;
        b.arrow = 'v';
        break;
    case DirLeft:
        b.x = 2;
        b.y = 23;
        b.w = 36;
        b.h = 18;
        b.arrow = '<';
        break;
    case DirRight:
        b.x = 90;
        b.y = 23;
        b.w = 36;
        b.h = 18;
        b.arrow = '>';
        break;
    }
    b.cx = b.x + b.w / 2;
    b.cy = b.y + b.h / 2;
    return b;
}

/* A small hint row with a dotted leader, e.g. "Rules .......... <" */
static void draw_dotted_row(Canvas* canvas, int16_t y, const char* label, const char* key) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 6, y, label);
    uint16_t label_w = canvas_string_width(canvas, label);
    uint16_t key_w = canvas_string_width(canvas, key);
    int16_t dots_x = 6 + label_w + 3;
    int16_t dots_end = 122 - (int16_t)key_w - 2;
    for(int16_t x = dots_x; x < dots_end; x += 3) {
        canvas_draw_dot(canvas, x, y - 2);
    }
    canvas_draw_str_aligned(canvas, 122, y, AlignRight, AlignBottom, key);
}

/* Tiny 3px-high triangle used as scroll indicator (much smaller than a glyph). */
static void draw_scroll_arrow(Canvas* canvas, int16_t cx, int16_t cy, bool up) {
    for(int16_t k = 0; k < 3; k++) {
        int16_t row = up ? (cy - 1 + k) : (cy + 1 - k);
        canvas_draw_line(canvas, cx - k, row, cx + k, row);
    }
}

/* A solid triangle pointing in one of the 4 directions - used inside the
 * arrow fields so the direction reads at a glance instead of relying on a
 * small font glyph. Same technique as draw_scroll_arrow, just bigger and
 * in all 4 directions. */
static void draw_direction_arrow(Canvas* canvas, int16_t cx, int16_t cy, Direction d) {
    const int16_t size = 7; /* apex-to-base length / half base width */
    for(int16_t k = 0; k < size; k++) {
        switch(d) {
        case DirUp: {
            int16_t row = cy - size + 1 + k;
            canvas_draw_line(canvas, cx - k, row, cx + k, row);
            break;
        }
        case DirDown: {
            int16_t row = cy + size - 1 - k;
            canvas_draw_line(canvas, cx - k, row, cx + k, row);
            break;
        }
        case DirLeft: {
            int16_t col = cx - size + 1 + k;
            canvas_draw_line(canvas, col, cy - k, col, cy + k);
            break;
        }
        case DirRight: {
            int16_t col = cx + size - 1 - k;
            canvas_draw_line(canvas, col, cy - k, col, cy + k);
            break;
        }
        }
    }
}

/* Details in the otherwise empty corners around the cross: step progress on
 * the top left, the best level on the top right and twinkling stars at the
 * bottom. Everything stays inside the free corner areas so nothing touches
 * the arrow fields. */
static void draw_background(Canvas* canvas, GameApp* app) {
    /* twinkling stars over the whole screen: top and bottom corners */
    static const int16_t left_pts[10][2] = {
        {8, 52}, {18, 58}, {30, 50}, {36, 57}, {12, 46},
        {6, 5},  {22, 17}, {36, 4},  {41, 15}, {14, 20}};
    static const int16_t right_pts[10][2] = {
        {120, 52}, {110, 58}, {98, 50}, {92, 57}, {116, 46},
        {122, 5},  {104, 17}, {92, 4},  {87, 15}, {112, 20}};

    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontSecondary);

    bool show_progress = false;
    uint8_t done = 0;
    if(app->screen == ScreenInput) {
        show_progress = true;
        done = app->input_index;
    } else if(app->screen == ScreenWatching) {
        show_progress = true;
        done = app->watch_step;
    } else if(app->screen == ScreenRoundIntro) {
        show_progress = true;
    } else if(app->screen == ScreenRoundSuccess) {
        show_progress = true;
        done = app->level;
    }

    if(show_progress) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u/%u", done, app->level);
        canvas_draw_str_aligned(canvas, 6, 10, AlignLeft, AlignCenter, buf);

        if(app->scores_count > 0) {
            snprintf(buf, sizeof(buf), "Best %u", app->scores[0].level);
            canvas_draw_str_aligned(canvas, 122, 10, AlignRight, AlignCenter, buf);
        }
    }

    for(uint8_t i = 0; i < 10; i++) {
        if(((app->anim_tick / 8) + i) % 3 != 0) {
            canvas_draw_dot(canvas, left_pts[i][0], left_pts[i][1]);
        }
        if(((app->anim_tick / 8) + i * 2) % 3 != 0) {
            canvas_draw_dot(canvas, right_pts[i][0], right_pts[i][1]);
        }
    }
}

static void draw_cross(Canvas* canvas, GameApp* app) {
    draw_background(canvas, app);

    for(Direction d = DirUp; d <= DirLeft; d++) {
        BoxDef b = box_for(d);
        bool filled = false;
        bool wrong = false;

        if(app->screen == ScreenWatching && app->watch_sub == SubFlashOn &&
           app->pattern[app->watch_step] == d) {
            filled = true;
        }
        if(app->screen == ScreenInput && app->flash_input_feedback &&
           app->last_input_dir == d) {
            filled = true;
            wrong = app->input_was_wrong;
        }

        canvas_set_color(canvas, ColorBlack);
        if(filled) {
            canvas_draw_rbox(canvas, b.x, b.y, b.w, b.h, 4);
            canvas_set_color(canvas, ColorWhite);
        } else {
            /* double outline so the idle fields don't look like flat empty boxes */
            canvas_draw_rframe(canvas, b.x, b.y, b.w, b.h, 4);
            canvas_draw_rframe(canvas, b.x + 3, b.y + 3, b.w - 6, b.h - 6, 2);
        }

        draw_direction_arrow(canvas, b.cx, b.cy, d);

        if(wrong) {
            canvas_draw_line(canvas, b.x + 4, b.y + 4, b.x + b.w - 4, b.y + b.h - 4);
            canvas_draw_line(canvas, b.x + b.w - 4, b.y + 4, b.x + 4, b.y + b.h - 4);
        }

        canvas_set_color(canvas, ColorBlack);
    }
}

/* Rounded status panel centered in the free space of the cross.
 * The free area between the four arrow fields is x:38-90, y:20-44 - this
 * panel is deliberately inset a few pixels from that so it never touches
 * the arrow fields around it. Labels are kept short (<=5 chars) so they
 * never overflow the panel's interior width. */
static void draw_status_panel(Canvas* canvas, GameApp* app, const char* label, bool animate_dots) {
    const int16_t px = 41, py = 23, pw = 46, ph = 18;

    if(app->screen == ScreenRoundSuccess) {
        /* small blinking "sparkle" dots at the corners - stays fully inside
         * the free area so it never overlaps the arrow fields */
        if(((app->anim_tick / 4) % 2) == 0) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_dot(canvas, px - 2, py - 2);
            canvas_draw_dot(canvas, px + pw + 1, py - 2);
            canvas_draw_dot(canvas, px - 2, py + ph + 1);
            canvas_draw_dot(canvas, px + pw + 1, py + ph + 1);
        }
    }

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, px + 1, py + 1, pw - 2, ph - 2);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, px, py, pw, ph, 3);

    char buf[16];
    if(animate_dots) {
        uint8_t dots = (uint8_t)((app->anim_tick / 10) % 4);
        snprintf(buf, sizeof(buf), "%s%.*s", label, dots, "...");
    } else {
        snprintf(buf, sizeof(buf), "%s", label);
    }
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, py + 5, AlignCenter, AlignCenter, buf);

    if(app->level > 0) {
        char lvl[12];
        snprintf(lvl, sizeof(lvl), "Lv.%u", app->level);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, py + 13, AlignCenter, AlignCenter, lvl);
    }
}

static void draw_menu(Canvas* canvas, GameApp* app) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 2, 2, 124, 60, 4);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignCenter, "MEMORY GAME");
    canvas_draw_line(canvas, 16, 15, 112, 15);

    /* tiny twinkling dots beside the title */
    if(((app->anim_tick / 10) % 2) == 0) {
        canvas_draw_dot(canvas, 10, 8);
        canvas_draw_dot(canvas, 117, 12);
    } else {
        canvas_draw_dot(canvas, 12, 12);
        canvas_draw_dot(canvas, 115, 8);
    }

    /* selectable Start / Settings rows, navigated with Up/Down.
     * Boxes are 13px tall so the bold label sits centered with air above/below. */
    const char* items[2] = {"OK - START", "SETTINGS"};
    const int16_t item_y[2] = {18, 32};
    const int16_t item_h = 13;
    for(uint8_t i = 0; i < 2; i++) {
        bool selected = (app->menu_selection == i);
        canvas_set_color(canvas, ColorBlack);
        if(selected) {
            canvas_draw_rbox(canvas, 20, item_y[i], 88, item_h, 3);
            canvas_set_color(canvas, ColorWhite);
        } else {
            canvas_draw_rframe(canvas, 20, item_y[i], 88, item_h, 3);
        }
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas, 64, item_y[i] + item_h / 2 + 1, AlignCenter, AlignCenter, items[i]);
        canvas_set_color(canvas, ColorBlack);
    }

    /* 8px apart so the two hint rows never touch each other, and the lower
     * one keeps a pixel of air above the frame border */
    canvas_set_font(canvas, FontSecondary);
    draw_dotted_row(canvas, 52, "Rules", "<");
    draw_dotted_row(canvas, 60, "Highscore", ">");
}

static void draw_settings(Canvas* canvas, GameApp* app) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 2, 2, 124, 60, 4);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignCenter, "SETTINGS");
    canvas_draw_line(canvas, 16, 15, 112, 15);

    /* Scrolling list: only SETTINGS_VISIBLE (2) rows are drawn at a time and
     * the window follows the selection. The highlight boxes end well above
     * the "Back" row, and are narrower than the frame so the scroll arrows
     * fit beside them. */
    const int16_t row_y[SETTINGS_VISIBLE] = {26, 41};
    const int16_t row_h = 13;
    const int16_t box_x = 6, box_w = 104, text_x = 58;

    canvas_set_font(canvas, FontSecondary);
    for(uint8_t slot = 0; slot < SETTINGS_VISIBLE; slot++) {
        uint8_t i = app->settings_scroll + slot;
        if(i >= SETTINGS_ITEM_COUNT) break;

        bool selected = (app->settings_selection == i);
        canvas_set_color(canvas, ColorBlack);
        if(selected) {
            canvas_draw_rbox(canvas, box_x, row_y[slot] - row_h / 2, box_w, row_h, 3);
            canvas_set_color(canvas, ColorWhite);
            /* small blinking cursor next to the active row */
            if(((app->anim_tick / 8) % 2) == 0) {
                canvas_draw_str_aligned(canvas, 10, row_y[slot], AlignLeft, AlignCenter, ">");
            }
        }

        if(i == SETTINGS_VOLUME) {
            /* slider row: label, a bar track with a filled portion, and the
             * percentage, all on the row's single center line */
            char pct[6];
            snprintf(pct, sizeof(pct), "%u%%", app->volume);
            canvas_draw_str_aligned(
                canvas, box_x + 4, row_y[slot], AlignLeft, AlignCenter, "Vol");

            const int16_t bar_x = 34, bar_w = 46, bar_h = 6;
            int16_t bar_y = row_y[slot] - bar_h / 2;
            canvas_draw_frame(canvas, bar_x, bar_y, bar_w, bar_h);
            int16_t fill_w = (bar_w - 2) * app->volume / 100;
            if(fill_w > 0) canvas_draw_box(canvas, bar_x + 1, bar_y + 1, fill_w, bar_h - 2);
            /* small blinking tick right at the fill edge while adjusting */
            if(selected && ((app->anim_tick / 6) % 2) == 0 && app->volume > 0 &&
               app->volume < 100) {
                canvas_draw_line(
                    canvas,
                    bar_x + 1 + fill_w,
                    bar_y - 1,
                    bar_x + 1 + fill_w,
                    bar_y + bar_h);
            }

            canvas_draw_str_aligned(
                canvas, box_x + box_w - 4, row_y[slot], AlignRight, AlignCenter, pct);
        } else {
            char text[24];
            if(i == SETTINGS_RESET && app->toast_timer_ms > 0) {
                snprintf(text, sizeof(text), "Cleared!");
            } else if(i == SETTINGS_SOUND) {
                snprintf(text, sizeof(text), "Sound: %s", app->sound_enabled ? "ON" : "OFF");
            } else if(i == SETTINGS_VIBRO) {
                snprintf(text, sizeof(text), "Vibration: %s", app->vibro_enabled ? "ON" : "OFF");
            } else if(i == SETTINGS_LED) {
                snprintf(text, sizeof(text), "LED: %s", app->led_enabled ? "ON" : "OFF");
            } else {
                snprintf(text, sizeof(text), "Reset Highscore");
            }
            canvas_draw_str_aligned(canvas, text_x, row_y[slot], AlignCenter, AlignCenter, text);
        }
        canvas_set_color(canvas, ColorBlack);
    }

    /* small arrows in the strip right of the boxes: down at the top, both in
     * the middle, up at the bottom */
    if(app->settings_scroll > 0) {
        draw_scroll_arrow(canvas, 118, 24, true);
    }
    if(app->settings_scroll < SETTINGS_ITEM_COUNT - SETTINGS_VISIBLE) {
        draw_scroll_arrow(canvas, 118, 42, false);
    }

    draw_dotted_row(canvas, 58, "Back", "<");
}

static void draw_rules(Canvas* canvas, GameApp* app) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 2, 2, 124, 60, 4);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignCenter, "HOW TO PLAY");
    canvas_draw_line(canvas, 16, 15, 112, 15);

    canvas_set_font(canvas, FontSecondary);
    uint8_t max_scroll = RULES_LINE_COUNT > RULES_VISIBLE ? RULES_LINE_COUNT - RULES_VISIBLE : 0;
    if(app->rules_scroll > max_scroll) app->rules_scroll = max_scroll;

    int16_t y = 24;
    for(uint8_t i = 0; i < RULES_VISIBLE && (app->rules_scroll + i) < RULES_LINE_COUNT; i++) {
        canvas_draw_str(canvas, 8, y, rules_lines[app->rules_scroll + i]);
        y += 8;
    }

    /* small static scroll arrows on the right, kept clear of the divider and
     * of the "Back" row: top = only down, middle = both, bottom = only up */
    if(app->rules_scroll > 0) {
        draw_scroll_arrow(canvas, 119, 21, true);
    }
    if(app->rules_scroll < max_scroll) {
        draw_scroll_arrow(canvas, 119, 46, false);
    }

    draw_dotted_row(canvas, 58, "Back", "<");
}

static void draw_leaderboard(Canvas* canvas, GameApp* app) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 2, 2, 124, 60, 4);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignCenter, "HIGHSCORE");
    canvas_draw_line(canvas, 16, 15, 112, 15);

    if(app->scores_count == 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "No runs yet -");
        canvas_draw_str_aligned(canvas, 64, 41, AlignCenter, AlignCenter, "play a game!");
    } else {
        uint8_t max_scroll = app->scores_count > LEADERBOARD_VISIBLE ?
                                  app->scores_count - LEADERBOARD_VISIBLE :
                                  0;
        if(app->leaderboard_scroll > max_scroll) app->leaderboard_scroll = max_scroll;

        /* Rank badge is an 11px disc (radius 5) so the digit sits fully
         * inside it with a clear margin; rows are 11px apart. */
        int16_t y = 23;
        canvas_set_font(canvas, FontSecondary);
        for(uint8_t i = 0;
            i < LEADERBOARD_VISIBLE && (app->leaderboard_scroll + i) < app->scores_count;
            i++) {
            uint8_t idx = app->leaderboard_scroll + i;

            canvas_set_color(canvas, ColorBlack);
            canvas_draw_disc(canvas, 15, y, 5);
            canvas_set_color(canvas, ColorWhite);
            char rank[4];
            snprintf(rank, sizeof(rank), "%u", idx + 1);
            /* Baseline at y+4 (on the device y+3 looked one pixel too
             * high). Horizontally centered from the measured digit width. */
            uint16_t rank_w = canvas_string_width(canvas, rank);
            canvas_draw_str(canvas, 15 - (int16_t)rank_w / 2, y + 4, rank);
            canvas_set_color(canvas, ColorBlack);

            char line[16];
            snprintf(line, sizeof(line), "Level %u", app->scores[idx].level);
            canvas_draw_str_aligned(canvas, 28, y, AlignLeft, AlignCenter, line);
            if(app->scores[idx].count > 1) {
                char times[8];
                snprintf(times, sizeof(times), "%ux", app->scores[idx].count);
                canvas_draw_str_aligned(canvas, 110, y, AlignRight, AlignCenter, times);
            }
            y += 11;
        }

        /* 3 rows are visible at once so rank 3 stays clear of the "Back" row;
         * the small arrows sit in the strip to the right of the rows */
        if(app->leaderboard_scroll > 0) {
            draw_scroll_arrow(canvas, 119, 22, true);
        }
        if(app->leaderboard_scroll < max_scroll) {
            draw_scroll_arrow(canvas, 119, 44, false);
        }
    }

    draw_dotted_row(canvas, 59, "Back", "<");
}

static void draw_confirm_exit(Canvas* canvas, GameApp* app) {
    /* Frozen game board in the background */
    draw_cross(canvas, app);
    draw_status_panel(canvas, app, "PAUSE", false);

    canvas_set_color(canvas, ColorWhite);
    canvas_draw_rbox(canvas, 12, 18, 104, 32, 4);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 12, 18, 104, 32, 4);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 27, AlignCenter, AlignCenter, "Quit game?");

    canvas_set_font(canvas, FontSecondary);
    if(app->confirm_yes) {
        canvas_draw_rbox(canvas, 22, 35, 34, 13, 3);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(canvas, 39, 42, AlignCenter, AlignCenter, "Yes");
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_rframe(canvas, 68, 35, 34, 13, 3);
        canvas_draw_str_aligned(canvas, 85, 42, AlignCenter, AlignCenter, "No");
    } else {
        canvas_draw_rframe(canvas, 22, 35, 34, 13, 3);
        canvas_draw_str_aligned(canvas, 39, 42, AlignCenter, AlignCenter, "Yes");
        canvas_draw_rbox(canvas, 68, 35, 34, 13, 3);
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_str_aligned(canvas, 85, 42, AlignCenter, AlignCenter, "No");
        canvas_set_color(canvas, ColorBlack);
    }
}

static void draw_game_over(Canvas* canvas, GameApp* app) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_rframe(canvas, 2, 2, 124, 60, 4);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignCenter, "GAME OVER");
    canvas_draw_line(canvas, 16, 15, 112, 15);

    /* tiny twinkling dots beside the title */
    if(((app->anim_tick / 10) % 2) == 0) {
        canvas_draw_dot(canvas, 10, 8);
        canvas_draw_dot(canvas, 117, 12);
    } else {
        canvas_draw_dot(canvas, 12, 12);
        canvas_draw_dot(canvas, 115, 8);
    }

    /* Side columns (x 6-34 and 94-122, y 18-36) are free of text and the
     * button: falling confetti after a new record, a calm twinkle otherwise */
    for(uint8_t k = 0; k < 6; k++) {
        int16_t lx = 6 + (k * 5) % 28;
        int16_t rx = 94 + (k * 5 + 3) % 28;
        if(app->was_new_record) {
            int16_t fy = 18 + (int16_t)((app->anim_tick / 2 + k * 4) % 18);
            canvas_draw_dot(canvas, lx, fy);
            canvas_draw_dot(canvas, rx, 36 - (fy - 18));
        } else if(((app->anim_tick / 10) + k) % 3 == 0) {
            canvas_draw_dot(canvas, lx, 20 + (k * 3) % 15);
            canvas_draw_dot(canvas, rx, 22 + (k * 4) % 13);
        }
    }

    canvas_set_font(canvas, FontSecondary);
    if(app->was_new_record) {
        /* blinking "new record" banner - small font so it always fits */
        if(((app->anim_tick / 8) % 2) == 0) {
            canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignCenter, "* NEW RECORD *");
        }
    } else {
        canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignCenter, "Level reached");
    }

    char lvl[16];
    snprintf(lvl, sizeof(lvl), "Level %u", app->final_level);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, lvl);

    /* button ends at y=49, hint row is centered at y=56 - clear gap between */
    /* the button pulses between filled and outlined so it clearly reads as
     * something you can press, not as plain text */
    canvas_set_font(canvas, FontSecondary);
    if(((app->anim_tick / 12) % 2) == 0) {
        canvas_draw_rbox(canvas, 14, 38, 100, 12, 4);
        canvas_set_color(canvas, ColorWhite);
    } else {
        canvas_draw_rframe(canvas, 14, 38, 100, 12, 4);
    }
    canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "OK - New Game");
    canvas_set_color(canvas, ColorBlack);

    canvas_draw_str_aligned(canvas, 12, 56, AlignLeft, AlignCenter, "< Menu");
    canvas_draw_str_aligned(canvas, 116, 56, AlignRight, AlignCenter, "Score >");
}

static void render_callback(Canvas* canvas, void* ctx) {
    GameApp* app = ctx;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    switch(app->screen) {
    case ScreenMenu:
        draw_menu(canvas, app);
        break;
    case ScreenSettings:
        draw_settings(canvas, app);
        break;
    case ScreenRules:
        draw_rules(canvas, app);
        break;
    case ScreenLeaderboard:
        draw_leaderboard(canvas, app);
        break;
    case ScreenRoundIntro:
        draw_cross(canvas, app);
        draw_status_panel(canvas, app, "WAIT", true);
        break;
    case ScreenWatching:
        draw_cross(canvas, app);
        draw_status_panel(canvas, app, "WAIT", true);
        break;
    case ScreenInput:
        draw_cross(canvas, app);
        draw_status_panel(canvas, app, "GO!", false);
        break;
    case ScreenRoundSuccess:
        draw_cross(canvas, app);
        draw_status_panel(canvas, app, "NICE!", false);
        break;
    case ScreenConfirmExit:
        draw_confirm_exit(canvas, app);
        break;
    case ScreenGameOver:
        draw_game_over(canvas, app);
        break;
    }
}

static void input_callback(InputEvent* event, void* ctx) {
    GameApp* app = ctx;
    GameEvent evt = {.type = EventTypeKey, .input = *event};
    furi_message_queue_put(app->queue, &evt, FuriWaitForever);
}

static void timer_callback(void* ctx) {
    GameApp* app = ctx;
    GameEvent evt = {.type = EventTypeTick};
    furi_message_queue_put(app->queue, &evt, 0);
}

/* ---------- App lifecycle ---------- */

static GameApp* game_app_alloc(void) {
    GameApp* app = malloc(sizeof(GameApp));
    memset(app, 0, sizeof(GameApp));

    app->screen = ScreenMenu;
    app->return_screen = ScreenMenu;
    app->running = true;

    app->queue = furi_message_queue_alloc(8, sizeof(GameEvent));
    app->notification = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);

    load_scores(app);
    load_settings(app);

    return app;
}

static void game_app_free(GameApp* app) {
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);
    furi_message_queue_free(app->queue);
    free(app);
}

int32_t memory_game_app(void* p) {
    UNUSED(p);
    GameApp* app = game_app_alloc();

    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, render_callback, app);
    view_port_input_callback_set(view_port, input_callback, app);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    app->timer = furi_timer_alloc(timer_callback, FuriTimerTypePeriodic, app);
    furi_timer_start(app->timer, furi_ms_to_ticks(TICK_MS));

    GameEvent event;
    while(app->running) {
        if(furi_message_queue_get(app->queue, &event, FuriWaitForever) == FuriStatusOk) {
            if(event.type == EventTypeKey) {
                handle_input(app, &event.input);
            } else if(event.type == EventTypeTick) {
                handle_tick(app);
            }
        }
        view_port_update(view_port);
    }

    tone_stop(app);
    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);

    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);

    game_app_free(app);
    return 0;
}

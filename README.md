# Memory Game – Flipper Zero App

A Simon-Says style memory game for the Flipper Zero, controlled entirely
with the 4 arrow keys + OK/Back.

## Controls

- **Title screen:** Up/Down = select Start/Settings, OK = confirm,
  Left = Rules, Right = Highscore
- **Watch phase:** the sequence lights up automatically (field turns black,
  LED blinks blue for every step)
- **Your turn:** just press the arrow key that lit up - no OK needed,
  the key press itself is the guess. LED blinks green when correct, red
  when wrong.
- **Back (short):** in-game → "Quit game?" confirmation, on info screens →
  go back one step
- **Back (hold 1 second):** instantly ends the current run
- **Game over:** Left = Menu, OK = New game, Back = Menu, Right = Highscore

The **top 5 all-time results** are saved automatically to the SD card and
re-sorted after every run. Settings (Sound/Vibration/LED) can be toggled
independently, and the highscore list can be reset there too.

## Build instructions (ufbt)

Python and `ufbt` are already installed on this PC.

Build (and copy the `.fap` into `dist/`):

```bash
python -m ufbt
```

Build **and** launch directly on a connected Flipper:

```bash
python -m ufbt launch
```

The finished app is at `dist/memory_game.fap` - copy it via qFlipper to
`apps/Games/` on the Flipper's SD card, or use `launch` to skip that step
entirely.

## Files

- `application.fam` – app manifest (name, category, entry point)
- `memory_game.c` – full game logic + UI (single file)

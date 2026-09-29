<div align="center">

# 🧠 Memory Game for Flipper Zero

A Simon-Says style memory game — watch the pattern, repeat it, and see how
far you get. Written in C for the Flipper Zero.

![Platform](https://img.shields.io/badge/platform-Flipper%20Zero-orange)
![Language](https://img.shields.io/badge/language-C-blue)
![License](https://img.shields.io/badge/license-MIT-green)

</div>

---

## What is this?

This is a classic "Simon Says" memory game, built specifically for the
Flipper Zero's little screen and D-pad. Four arrow fields light up in a
sequence you have to watch closely — then you repeat that exact sequence
by pressing the matching arrow keys. Get it right and the sequence grows
by one more step; get it wrong and the game ends. How far can you get?

It's a single self-contained app (`memory_game.c`) with no dependencies
beyond the Flipper's own firmware — no name entry, no setup, just pick it
from the app list and play.

## ✨ Features

- **Simon-Says gameplay** — a pattern of arrows lights up, you repeat it;
  every round adds one more step
- **No extra confirmation needed** — pressing an arrow key *is* your guess,
  no OK button required
- **Color-coded feedback** — the RGB LED turns blue while the pattern
  plays, green for a correct guess, red for a wrong one, held for exactly
  as long as the matching tone plays
- **Persistent Top-5 Highscore** — automatically saved to the SD card and
  re-sorted after every run; reaching a level you've already hit before
  just bumps its "×N" counter instead of taking a new spot
- **Settings screen** — toggle sound, vibration and LED feedback
  independently, adjust the volume with a slider, and reset the highscore
- **Retro game-over jingle** — a descending run of 6 notes when you lose a
  run, just two short notes when you quit on purpose
- **Polished UI** — rounded panels, real arrow shapes in the gameplay
  fields, scroll indicators, small idle animations (twinkling stars, a
  pulsing button, falling confetti on a new record)

## 📥 Installation

### Option A — Install the ready-made app

1. Download **`memory_game.fap`** from this folder.
2. Install [qFlipper](https://flipperzero.one/update) if you don't have it
   yet, and connect your Flipper Zero via USB.
3. Open qFlipper → **File Manager**.
4. Copy `memory_game.fap` into `SD Card/apps/Games/` (create the `Games`
   folder if it doesn't exist yet).
5. On the Flipper: **Menu → Apps → Games → Memory Game**.

That's it — no compiling, no extra tools needed.

### Option B — Build it yourself from source

You'll need [Python 3](https://www.python.org/downloads/) and
[`ufbt`](https://github.com/flipperdevices/flipperzero-ufbt) (the micro
Flipper Build Tool).

```bash
pip install ufbt
```

Then, from this folder:

```bash
python -m ufbt          # builds dist/memory_game.fap
python -m ufbt launch   # builds AND installs/launches it on a connected Flipper
```

## 🎮 Controls

| Screen | Input | Action |
|---|---|---|
| Title screen | `Up`/`Down` | Select Start / Settings |
| | `Left` / `Right` | Rules / Highscore |
| | `OK` | Confirm selection |
| Settings | `Up`/`Down` | Select a row |
| | `Left`/`Right` (on Volume) | Adjust volume |
| | `Left` (elsewhere) / `Back` | Back to menu |
| Watching phase | — | Just watch the pattern |
| Your turn | `Up`/`Down`/`Left`/`Right` | Guess the next step (no OK needed) |
| Any screen | `Back` (short) | Go back / "Quit game?" prompt during a run |
| Any screen | `Back` (hold 1s) | Instantly end the current run |
| Game over | `Left` / `Back` | Back to menu |
| | `OK` | New game |
| | `Right` | View highscore |

## 🗂 Project structure

```
memory-game/
├── application.fam    # App manifest (name, category, entry point)
├── memory_game.c       # Full game logic + UI (single file)
├── icon.png             # 10x10 app list icon
├── make_icon.py         # Small script that generated icon.png
├── memory_game.fap      # Ready-to-install build (see Installation)
├── LICENSE
└── README.md
```

## 🛠 Built with

- [ufbt](https://github.com/flipperdevices/flipperzero-ufbt) — micro Flipper
  Build Tool
- The official [Flipper Zero firmware](https://github.com/flipperdevices/flipperzero-firmware)
  GUI/notification/storage APIs

## 📄 License

MIT — see [LICENSE](LICENSE).

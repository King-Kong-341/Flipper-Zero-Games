<div align="center">

# 🧠 Memory Game for Flipper Zero

A Simon-Says style memory game — watch the pattern, repeat it, and see how
far you get. Built as a native Flipper Zero app in C.

![Platform](https://img.shields.io/badge/platform-Flipper%20Zero-orange)
![Language](https://img.shields.io/badge/language-C-blue)
![License](https://img.shields.io/badge/license-MIT-green)

<!--
  Screenshots go here once available — see assets/screenshots/.
  Example once filled in:
  <img src="assets/screenshots/menu.png" width="200"/>
  <img src="assets/screenshots/gameplay.png" width="200"/>
  <img src="assets/screenshots/highscore.png" width="200"/>
-->

</div>

---

## ✨ Features

- **Simon-Says gameplay** — a pattern of arrows lights up, you repeat it;
  every round adds one more step
- **No extra confirmation needed** — pressing an arrow key *is* your guess,
  no OK button required
- **Color-coded feedback** — the RGB LED turns blue while the pattern plays,
  green for a correct guess, red for a wrong one
- **Persistent Top-5 Highscore** — automatically saved to the SD card and
  re-sorted after every run, with a scrollable list
- **Settings screen** — toggle sound, vibration and LED feedback
  independently, plus a highscore reset
- **Polished UI** — rounded panels, scroll indicators, small idle
  animations and a retro descending "game over" jingle

## 📥 Installation

### Option A — Install the ready-made app

1. Download `memory_game.fap` from this repository (see the
   [Releases](../../releases) page, or the `dist/` folder if building
   yourself).
2. Install [qFlipper](https://flipperzero.one/update) if you don't have it
   yet, and connect your Flipper Zero via USB.
3. Open qFlipper → **File Manager**.
4. Copy `memory_game.fap` into `SD Card/apps/Games/`.
5. On the Flipper: **Menu → Apps → Games → Memory Game**.

### Option B — Build it yourself from source

You'll need [Python 3](https://www.python.org/downloads/) and
[`ufbt`](https://github.com/flipperdevices/flipperzero-ufbt) (the micro
Flipper Build Tool).

```bash
pip install ufbt
```

Then, from the `memory_game` folder:

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
| Watching phase | — | Just watch the pattern |
| Your turn | `Up`/`Down`/`Left`/`Right` | Guess the next step (no OK needed) |
| Any screen | `Back` (short) | Go back / "Quit game?" prompt during a run |
| Any screen | `Back` (hold 1s) | Instantly end the current run |
| Game over | `Left` | Back to menu |
| | `OK` | New game |
| | `Right` | View highscore |

## 🗂 Project structure

```
memory_game/
├── application.fam     # App manifest (name, category, entry point)
├── memory_game.c        # Full game logic + UI (single file)
├── icon.png              # 10x10 app list icon
├── make_icon.py          # Small Pillow script that generated icon.png
└── dist/                 # Build output (memory_game.fap) — not tracked in git
```

## 🛠 Built with

- [ufbt](https://github.com/flipperdevices/flipperzero-ufbt) — micro Flipper
  Build Tool
- The official [Flipper Zero firmware](https://github.com/flipperdevices/flipperzero-firmware)
  GUI/notification/storage APIs

## 📄 License

MIT — see [LICENSE](LICENSE).

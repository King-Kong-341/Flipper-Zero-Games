<div align="center">

# 🧠 Memory Game for Flipper Zero

A Simon-Says style memory game — watch the pattern, repeat it, and see how
far you get.

![Platform](https://img.shields.io/badge/platform-Flipper%20Zero-orange)
![License](https://img.shields.io/badge/license-MIT-green)

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

You'll need [qFlipper](https://flipperzero.one/update), the official
Flipper Zero desktop app, to transfer files to your device.

**Step by step:**

1. Download **`memory_game.fap`** from this repository (green **Code**
   button → or directly from the file list above).
2. Install qFlipper if you don't have it yet:
   [flipperzero.one/update](https://flipperzero.one/update)
3. Connect your Flipper Zero to your computer via USB.
4. Open qFlipper. It should show your Flipper as connected.
5. In qFlipper, click **File Manager** (the folder icon on the left).
6. Navigate to `SD Card` → `apps` → `Games`.
   (If the `Games` folder doesn't exist, create it.)
7. Drag and drop `memory_game.fap` into that folder.
8. On the Flipper Zero itself: **Menu → Apps → Games → Memory Game**.

That's it — no compiling, no extra tools needed.

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

## 📄 License

MIT — see [LICENSE](LICENSE). Free to use and share.

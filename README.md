# Hide & Seek for Flipper Zero

Turn your Flipper Zero into a hide-and-seek toolkit with three game modes using its speaker, vibration motor, RGB LED, and NFC reader.

## Modes

### 1. Find the Flipper (Beacon)
Hide the Flipper itself and let seekers track it down by sound, vibration, and LED flashes.
- **Hide Delay:** 5–60 seconds (5s increments, default 15s) to stash the Flipper before it arms.
- **Round Timer:** 1:00–10:00 (30s increments, default 3:00).
- **Escalating Chirps:** Beeps and flashes every 15s, tightening to every 7s in the final minute and every 3s in the final 20 seconds.
- **Nonstop Timeout Alarm:** If time runs out before anyone finds it, the Flipper triggers an urgent alarm every 2 seconds until someone finds it and presses **OK** to silence and reset.

### 2. Seeker Deck (Timer & Horn)
A handheld referee tool for classic hide-and-seek rounds.
- **Countdown Timer:** 10–120 seconds (default 30s) with ticking alerts in the final 5 seconds and a start fanfare at zero.
- **Hider Counter:** Tracks 1–10 hiders (default 4) and triggers a victory chime once everyone is found.
- **Marco Polo Horn:** Press **OK** anytime while seeking to blast a two-tone horn.

### 3. NFC Tag-Out / Home Base
Pair cheap ISO14443-A NFC tags (NTAG stickers, wristbands, keyfobs, or hotel cards) with players.
- **Supports up to 8 players:** Tap tags to the back of the Flipper during setup to register each player's UID.
- **Two ways to play:** Carry the Flipper as the seeker to scan hiders as you find them, or leave it at Home Base so hiders can sneak back and tap in.
- **Rematch Ready:** Keeps registered tags loaded between rounds (**OK** for rematch, **Down** to clear and re-register).

---

## Controls

| Mode | Setup | In-Game | End Screen |
| :--- | :--- | :--- | :--- |
| **Find the Flipper** | **Up/Down:** Hide delay<br>**Left/Right:** Round time<br>**OK:** Arm beacon | **OK:** Disarm (Found!)<br>**Back:** Cancel to menu | **OK:** Silence & reset |
| **Seeker Deck** | **Up/Down:** Countdown<br>**Left/Right:** Hider count<br>**OK:** Start countdown | **Up:** Mark found (+1)<br>**Down:** Undo (-1)<br>**OK:** Marco Polo horn | **OK:** Next round |
| **NFC Tag-Out** | **Tap Tag:** Register player<br>**Down:** Clear list<br>**OK:** Start round | **Tap Tag:** Tag out / check in<br>**OK:** Marco Polo horn | **OK:** Rematch<br>**Down:** Reset tags |

---

## Build & Install

Requires [uFBT](https://github.com/flipperdevices/flipperzero-ufbt).

### Compile Only
```bash
ufbt
```
The compiled `.fap` file is output to `dist/hide_and_seek.fap`. Copy it to `/ext/apps/Games/` on your Flipper Zero SD card.

### Compile & Launch Over USB
Close qFlipper and any browser tabs connected to `lab.flipper.net`, connect your Flipper Zero via USB, and run:

```bash
# macOS / Linux
ufbt launch

# Windows PowerShell
py -m ufbt launch
```

## Project Structure

```text
hide_and_seek/
├── application.fam   # Flipper app manifest (Games category)
├── hide_and_seek.c   # App logic, UI views, notifications, and NFC poller
├── hide_and_seek.png # Flipper App Image
├── LICENSE           # MIT License
└── README.md         # Project documentation
```

## v0.3 - 2026-11-24

### Added
- Nonstop repeating timeout alarm (every 2 seconds) in **Find the Flipper** mode so the beacon keeps alerting until found and silenced.
- Updated timeout screen with `[OK] Silence & Reset`.

### Removed
- Removed experimental Sub-GHz Radar mode to prevent device freezes and improve stability.

---

## v0.2 - 2026-11-24

### Added
- **NFC Tag-Out / Home Base** mode supporting up to 8 ISO14443-A NFC tags (NTAG stickers, wristbands, keyfobs, and cards) with per-player tag times and instant rematch support.
- Increased stack size to 4 KB for NFC polling headroom.

---

## v0.1 - 2026-11-24

### Added
- Initial build of **Hide & Seek** for Flipper Zero.
- **Find the Flipper (Beacon)** mode with configurable hide delay (5–60s), round timer (1–10m), and escalating sound, LED, and vibration chirps (15s → 7s → 3s).
- **Seeker Deck** mode with countdown timer (10–120s), hider counter (1–10 players), and a Marco Polo horn.

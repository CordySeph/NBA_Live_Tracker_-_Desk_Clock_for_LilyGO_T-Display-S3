# 🏀 LilyGO T-Display-S3 — NBA Live Tracker & Desk Clock ⏰

An advanced, real-time NBA scoreboard, standby desk clock, next-game countdown monitor, and conference standings dashboard designed for the **LilyGO T-Display-S3 (Non-Touch)** ESP32-S3 board (1.9-inch 170x320 IPS display).

---

## 🌟 Key Highlights

- **🌐 Multi-Location Wi-Fi Memory (NVS Flash)**: 
  - Remembers multiple Wi-Fi networks (e.g., Home 1, Home 2, Office, Mobile Hotspot) permanently in flash memory.
  - Automatically reconnects to whichever known network is in range.
  - **Zero re-flashing or re-configuration needed** when moving back and forth between previously connected locations.
- **📱 On-Demand Phone Web Setup (Captive Portal)**:
  - If a new/unknown Wi-Fi is detected (or by holding the BOOT button for 3 seconds), the device broadcasts `NBA-Display-Setup`.
  - Connect with your smartphone, pick your local Wi-Fi, enter the password, optionally pick your favorite team, and tap **Save & Connect**!
- **⏰ Smart Standby Desk Clock Mode (Idea 1 + Idea 4 Dark Edition)**:
  - Large digital clock with live ticking seconds (`:xx`) synchronized via **NTP**.
  - **Next Match Countdown**: Displays the upcoming matchup, tip-off time in your local timezone, and countdown badge (e.g., `In 2d 12h`).
  - **Last Game Recap**: Displays the final score and result of the most recently finished match.
  - Conference standing badge for your favorite team (e.g. `#5 WEST • 1-1`).
- **🏀 Favorite Team Spotlight Mode**:
  - Full side-by-side box score with big team names, records, and scores.
  - Real-time quarter/clock badge (`LIVE 9:20 2nd`, `Halftime`, or `FINAL`).
  - **Supports all 30 NBA teams!** (Default: Golden State Warriors - GSW).
- **📅 NBA Today (League Scoreboard)**:
  - Lists all matches scheduled for the day (3 matches per page).
  - Matches featuring your favorite team are highlighted with a gold border.
- **📊 Conference Standings (West & East)**:
  - Live ranking table showing Conference Seed (**RK**), Team Name (**TEAM**), Win-Loss (**W-L**), and Games Behind (**GB**).
- **🤖 Smart Display Automation**:
  - Automatically switches to the **Live Scoreboard** during a live match.
  - Automatically transitions to the **Standby Desk Clock & Countdown** when the game is over (`FINAL`) or on off-days.
- **⚡ High Performance & Zero Flicker**:
  - 16-bit double buffering using `TFT_eSprite` utilizing the onboard 8MB Octal PSRAM.

---

## 🕹️ Physical Button Controls

| Button | Pin / Hardware | Gesture | Function |
| :--- | :--- | :--- | :--- |
| **Side Button** | **GPIO 14** (Button 2) | **Single Press** | **Cycle Views**: Loops through screens:<br>1. ⏰ Standby Desk Clock & Next Match Countdown<br>2. 🏀 Favorite Team Match Detail (Full Scoreboard)<br>3. 📅 NBA Today (All League Games)<br>4. 📊 Western Conference Standings<br>5. 📊 Eastern Conference Standings |
| **BOOT Button** | **GPIO 0** (Button 1) | **Short Press** | **Force Refresh**: Immediately re-fetches scores and standings from the API. |
| **BOOT Button** | **GPIO 0** (Button 1) | **Long Press (3s)** | **Open Wi-Fi Setup Portal**: Broadcasts `NBA-Display-Setup` for mobile phone configuration. |

---

## 📱 How to Connect at a New House / Location (No Computer Required)

When you bring the device to a new house or location:

1. **Power on the device**:
   - If none of your previously saved Wi-Fi networks are detected, the screen automatically displays:
     - 📶 **SSID: NBA-Display-Setup**
     - 🌐 **http://192.168.4.1**
2. **Connect with your smartphone**:
   - On your phone, go to Wi-Fi settings and connect to **`NBA-Display-Setup`**.
3. **Configure in the web browser**:
   - The setup page will open automatically (or open your browser to `http://192.168.4.1`):
     - **Select Wi-Fi**: Tap your home network from the detected list.
     - **Enter Password**: Type the Wi-Fi password.
     - **Favorite Team**: You can change your favorite team directly from the dropdown.
     - Tap **`Save & Connect`**.
4. **Done!** The device restarts, connects to the new Wi-Fi, and saves it permanently to memory.
5. **Moving back to your previous house**: **Zero configuration required!** Just plug it in, and the device automatically detects and connects to your previous Wi-Fi.

> 💡 *Tip: You can manually open this setup portal anytime by **holding the BOOT button for 3 seconds**.*

---

## 🏀 How to Change Your Favorite Team

You can change your favorite team through either of the following two methods:

### Method A: Via Mobile Phone Setup Portal
1. Hold the **BOOT** button for 3 seconds to open the portal.
2. Connect your phone to `NBA-Display-Setup`.
3. Select your team from the **Favorite Team** dropdown (all 30 NBA teams available) and tap **Save & Connect**.

---

### Method B: Via Code Configuration File (`src/config.h`)
Open [`src/config.h`](file:///Users/cordyseph/Documents/GitHub/lilygo_t-display_s3/src/config.h) and update the `FAVORITE_TEAM` definition:

```cpp
#define FAVORITE_TEAM   "GS"  // Change to your desired team abbreviation
```

#### 📋 Complete 30 NBA Team Abbreviation Reference:

| Code | Team Name | Code | Team Name | Code | Team Name |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `"GS"` / `"GSW"` | **Golden State Warriors** | `"LAL"` | **Los Angeles Lakers** | `"BOS"` | **Boston Celtics** |
| `"MIL"` | **Milwaukee Bucks** | `"DEN"` | **Denver Nuggets** | `"DAL"` | **Dallas Mavericks** |
| `"MIA"` | **Miami Heat** | `"NY"` | **New York Knicks** | `"PHX"` | **Phoenix Suns** |
| `"CHI"` | **Chicago Bulls** | `"PHI"` | **Philadelphia 76ers** | `"LAC"` | **LA Clippers** |
| `"MIN"` | **Minnesota Timberwolves** | `"OKC"` | **Oklahoma City Thunder** | `"SAC"` | **Sacramento Kings** |
| `"IND"` | **Indiana Pacers** | `"CLE"` | **Cleveland Cavaliers** | `"ORL"` | **Orlando Magic** |
| `"MEM"` | **Memphis Grizzlies** | `"ATL"` | **Atlanta Hawks** | `"TOR"` | **Toronto Raptors** |
| `"BKN"` | **Brooklyn Nets** | `"HOU"` | **Houston Rockets** | `"SA"` | **San Antonio Spurs** |
| `"NO"` | **New Orleans Pelicans** | `"POR"` | **Portland Trail Blazers**| `"UTAH"` | **Utah Jazz** |
| `"DET"` | **Detroit Pistons** | `"CHA"` | **Charlotte Hornets** | `"WSH"` | **Washington Wizards** |

---

## ⚙️ Additional Settings (`src/config.h`)

You can customize additional default parameters in [`src/config.h`](file:///Users/cordyseph/Documents/GitHub/lilygo_t-display_s3/src/config.h):

```cpp
// Timezone offset in hours
#define TIMEZONE_OFFSET_HOURS 7  // UTC+7 for Thailand/Bangkok (UTC-5 for EST, UTC-8 for PST, etc.)

// Background update intervals in milliseconds
#define UPDATE_INTERVAL_NORMAL (30 * 1000)  // 30 seconds in normal time
#define UPDATE_INTERVAL_LIVE   (15 * 1000)  // 15 seconds during live games
```

---

## 🚀 How to Build and Upload via PlatformIO

If you modify code or settings and want to flash via USB:

### 1. Build Firmware
```bash
.venv/bin/pio run
```

### 2. Upload to Board
Connect your LilyGO T-Display-S3 via USB-C and execute:
```bash
.venv/bin/pio run -t upload
```

> **💡 ESP32-S3 Bootloader Tip:**  
> If PlatformIO shows `Connecting..........` or `Failed to connect`:
> 1. Press and hold the **BOOT** button (GPIO 0).
> 2. Press the **RST** button once and release it (while still holding BOOT).
> 3. Release the **BOOT** button.
> 4. Re-run `.venv/bin/pio run -t upload`.

### 3. Open Serial Monitor (Optional)
```bash
.venv/bin/pio run -t monitor
```
# support
```bash
BTC : bc1qg5z9z9cmuye7e8ls7qqc7szurecwxm6xqlg9mk
```

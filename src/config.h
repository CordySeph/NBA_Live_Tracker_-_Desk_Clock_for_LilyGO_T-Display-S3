#pragma once

// ==========================================
// WiFi Settings
// ==========================================
#define WIFI_SSID       "jib_2.4G"
#define WIFI_PASSWORD   "jib251830"

// ==========================================
// Timezone Settings
// ==========================================
#define TIMEZONE_OFFSET_HOURS 7  // UTC+7 (Bangkok / Thailand)

// ==========================================
// NBA Settings
// ==========================================
// Favorite team (Golden State Warriors = "GS" or "GSW")
#define FAVORITE_TEAM   "GS"

// Automatic data update intervals (in milliseconds)
#define UPDATE_INTERVAL_NORMAL (30 * 1000)  // 30 seconds when idle / off-hours
#define UPDATE_INTERVAL_LIVE   (15 * 1000)  // 15 seconds during live matches

// ==========================================
// Hardware Pinout for LilyGO T-Display-S3
// ==========================================
#define PIN_POWER_ON    15
#define PIN_LCD_BL      38
#define PIN_BUTTON_1    0   // BOOT Button (Press for Force Refresh, Hold 3s for Wi-Fi Setup)
#define PIN_BUTTON_2    14  // Side Button (Press to switch screens / pages)

#pragma once

// ==========================================
// WiFi Settings
// ==========================================
#define WIFI_SSID       "Your_WiFi_Name"
#define WIFI_PASSWORD   "Your_WiFi_Password"

// ==========================================
// Timezone Settings
// ==========================================
#define TIMEZONE_OFFSET_HOURS 7  // UTC+7 (Bangkok / Thailand)

// ==========================================
// NBA Settings
// ==========================================
// ทีมโปรด (Golden State Warriors = "GS" หรือ "GSW")
#define FAVORITE_TEAM   "GS"

// รอบเวลาอัปเดตข้อมูลอัตโนมัติ (หน่วย: มิลลิวินาที)
#define UPDATE_INTERVAL_NORMAL (30 * 1000)  // 30 วินาทีในเวลาปกติ
#define UPDATE_INTERVAL_LIVE   (15 * 1000)  // 15 วินาทีเมื่อมีแมตช์กำลังแข่งสด

// ==========================================
// Hardware Pinout for LilyGO T-Display-S3
// ==========================================
#define PIN_POWER_ON    15
#define PIN_LCD_BL      38
#define PIN_BUTTON_1    0   // BOOT Button (กดเพื่อ Force Refresh)
#define PIN_BUTTON_2    14  // Side Button (กดเพื่อสลับหน้า)

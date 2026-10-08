#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "nba_data.h"
#include "ui_display.h"
#include "wifi_manager.h"

UIDisplay ui;
NBAClient nba;
MultiWiFiManager wifiMgr;

// UI Modes:
// 0 = Standby Desk Clock & Countdown (หน้าพักจอ / นาฬิกาโต๊ะทำงาน)
// 1 = GSW Spotlight Match (ผลการแข่งขัน GSW เต็มจอ)
// 2 = All Games Today (หน้ารวมผลทุกคู่ประจำวัน)
// 3 = Standings / Rankings (อันดับตารางคะแนนสาย West & East)
int currentMode = 0;
int allGamesPageIndex = 0;
int standingsConfIndex = 0;
int standingsPageIndex = 0;

// Button debouncing
int lastBtn1State = HIGH;
int lastBtn2State = HIGH;
unsigned long lastBtn1DebounceTime = 0;
unsigned long lastBtn2DebounceTime = 0;
unsigned long btn1PressStart = 0;
const unsigned long DEBOUNCE_DELAY = 180; // ms

// Timer tracking
unsigned long lastUpdateAttempt = 0;
unsigned long lastClockTick = 0;

void refreshDisplay() {
    if (wifiMgr.portalRunning) {
        ui.drawPortalScreen("NBA-Display-Setup", "192.168.4.1");
        return;
    }

    if (currentMode == 0) {
        ui.drawStandbyClockPage(nba);
    } else if (currentMode == 1) {
        ui.drawGSWPage(nba.favoriteGame, nba.favoriteRank, nba.favoriteConference, nba.lastStatusMsg, nba.lastFetchTime);
    } else if (currentMode == 2) {
        int totalGames = nba.allGames.size();
        int totalPages = (totalGames + 2) / 3;
        if (totalPages == 0) totalPages = 1;
        if (allGamesPageIndex >= totalPages) allGamesPageIndex = 0;

        ui.drawAllGamesPage(nba.allGames, allGamesPageIndex, totalPages, nba.lastStatusMsg, nba.lastFetchTime);
    } else if (currentMode == 3) {
        if (!nba.conferences.empty()) {
            if (standingsConfIndex >= (int)nba.conferences.size()) standingsConfIndex = 0;
            const auto& conf = nba.conferences[standingsConfIndex];
            int totalPages = (conf.entries.size() + 3) / 4;
            if (totalPages == 0) totalPages = 1;
            if (standingsPageIndex >= totalPages) standingsPageIndex = 0;

            ui.drawStandingsPage(conf, standingsPageIndex, totalPages, nba.lastStatusMsg, nba.lastFetchTime);
        } else {
            currentMode = 0;
            ui.drawStandbyClockPage(nba);
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n--- LilyGO T-Display-S3 NBA Tracker ---");

    // Initialize buttons
    pinMode(PIN_BUTTON_1, INPUT_PULLUP);
    pinMode(PIN_BUTTON_2, INPUT_PULLUP);

    // Initialize LCD & Sprite
    ui.init();

    // Initialize Multi-WiFi Manager
    wifiMgr.init();
    nba.setFavoriteTeam(wifiMgr.favoriteTeam);

    // Check if BOOT button is held on power up -> force portal mode
    if (digitalRead(PIN_BUTTON_1) == LOW) {
        Serial.println("BOOT held: Starting Wi-Fi Portal...");
        wifiMgr.startPortal();
        refreshDisplay();
        return;
    }

    // Try auto-connecting to all saved Wi-Fi networks
    bool connected = wifiMgr.autoConnect([](const String& status) {
        ui.drawConnectingWiFi(status);
    });

    if (connected) {
        Serial.println("Wi-Fi connected! IP: " + WiFi.localIP().toString());
        nba.lastStatusMsg = "WiFi OK";

        // Sync real-time clock via NTP (Thailand UTC+7)
        configTime(TIMEZONE_OFFSET_HOURS * 3600, 0, "pool.ntp.org", "time.google.com");

        // Fetch scores & standings
        nba.updateData(true);

        // If game is live right now, start on GSW Spotlight; otherwise start on Standby Clock
        if (nba.hasLiveGame()) {
            currentMode = 1;
        } else {
            currentMode = 0;
        }
    } else {
        Serial.println("No saved Wi-Fi found. Starting Setup Portal...");
        wifiMgr.startPortal();
    }

    refreshDisplay();
}

void loop() {
    // ----------------------------------------------------
    // If Web Portal is active, service DNS and HTTP requests
    // ----------------------------------------------------
    if (wifiMgr.portalRunning) {
        wifiMgr.handlePortal();
        delay(10);
        return;
    }

    unsigned long now = millis();

    // ----------------------------------------------------
    // Button 2 (PIN 14): Cycle Through Pages
    // ----------------------------------------------------
    int reading2 = digitalRead(PIN_BUTTON_2);
    if (reading2 != lastBtn2State && (now - lastBtn2DebounceTime) > DEBOUNCE_DELAY) {
        lastBtn2DebounceTime = now;
        lastBtn2State = reading2;

        if (reading2 == LOW) { // Button pressed
            Serial.println("BTN2 pressed: Next mode");
            if (currentMode == 0) {
                // Clock -> GSW Match Detail
                currentMode = 1;
            } else if (currentMode == 1) {
                // GSW Match Detail -> All Games
                currentMode = 2;
                allGamesPageIndex = 0;
            } else if (currentMode == 2) {
                int totalGames = nba.allGames.size();
                int totalPages = (totalGames + 2) / 3;
                if (totalPages == 0) totalPages = 1;

                allGamesPageIndex++;
                if (allGamesPageIndex >= totalPages) {
                    // All Games done -> Standings
                    currentMode = 3;
                    standingsConfIndex = 0; // West first
                    standingsPageIndex = 0;
                }
            } else if (currentMode == 3) {
                if (!nba.conferences.empty()) {
                    const auto& conf = nba.conferences[standingsConfIndex];
                    int totalPages = (conf.entries.size() + 3) / 4;
                    if (totalPages == 0) totalPages = 1;

                    standingsPageIndex++;
                    if (standingsPageIndex >= totalPages) {
                        standingsPageIndex = 0;
                        standingsConfIndex++;
                        if (standingsConfIndex >= (int)nba.conferences.size()) {
                            // Standings done -> Back to Standby Clock
                            currentMode = 0;
                        }
                    }
                } else {
                    currentMode = 0;
                }
            }
            refreshDisplay();
        }
    }

    // ----------------------------------------------------
    // Button 1 (PIN 0 / BOOT): Force Refresh or Long-Press Setup
    // ----------------------------------------------------
    int reading1 = digitalRead(PIN_BUTTON_1);
    if (reading1 == LOW && lastBtn1State == HIGH) {
        btn1PressStart = now;
    }

    if (reading1 == LOW && (now - btn1PressStart > 3000)) { // 3 seconds long-press -> Wi-Fi setup
        Serial.println("Long press BTN1: Opening Wi-Fi setup portal...");
        wifiMgr.startPortal();
        refreshDisplay();
        return;
    }

    if (reading1 != lastBtn1State && (now - lastBtn1DebounceTime) > DEBOUNCE_DELAY) {
        lastBtn1DebounceTime = now;
        lastBtn1State = reading1;

        if (reading1 == HIGH && (now - btn1PressStart < 3000) && (now - btn1PressStart > 50)) { // Short press release
            Serial.println("BTN1 short press: Force refresh");
            nba.lastStatusMsg = "Updating...";
            refreshDisplay();

            nba.updateData(true);
            refreshDisplay();
        }
    }

    // ----------------------------------------------------
    // Automatic Background Polling for Scores
    // ----------------------------------------------------
    unsigned long interval = nba.hasLiveGame() ? UPDATE_INTERVAL_LIVE : UPDATE_INTERVAL_NORMAL;
    if (now - lastUpdateAttempt >= interval) {
        lastUpdateAttempt = now;

        // Auto-reconnect if dropped
        if (WiFi.status() != WL_CONNECTED) {
            wifiMgr.autoConnect();
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("Auto updating scores...");
            nba.updateData(false);
            refreshDisplay();
        }
    }

    // ----------------------------------------------------
    // Clock Ticking: If in Standby Clock Mode, update every 1 second
    // ----------------------------------------------------
    if (currentMode == 0) {
        if (now - lastClockTick >= 1000) {
            lastClockTick = now;
            refreshDisplay();
        }
    } else {
        if (now - lastClockTick >= 5000) {
            lastClockTick = now;
            refreshDisplay();
        }
    }

    delay(20);
}

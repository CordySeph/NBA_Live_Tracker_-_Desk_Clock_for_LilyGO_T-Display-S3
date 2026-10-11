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
// 0 = Standby Desk Clock & Countdown (Desk clock & upcoming match countdown)
// 1 = GSW Spotlight Match (Full box score for favorite team)
// 2 = All Games Today (Scores and schedules for all NBA games)
// 3 = Standings / Rankings (West & East conference standings)
int currentMode = 0;
int allGamesPageIndex = 0;
int standingsConfIndex = 0;
int standingsPageIndex = 0;
bool userNavigatedManually = false;

// Button debouncing
int lastBtn1State = HIGH;
int lastBtn2State = HIGH;
unsigned long lastBtn1DebounceTime = 0;
unsigned long lastBtn2DebounceTime = 0;
unsigned long btn1PressStart = 0;
const unsigned long DEBOUNCE_DELAY = 180; // ms

// Timer tracking for UI
unsigned long lastClockTick = 0;

// FreeRTOS background task on Core 0
TaskHandle_t nbaTaskHandle = nullptr;
volatile bool requestForceUpdate = false;
volatile bool needsDisplayRefresh = false;

void refreshDisplay() {
    if (wifiMgr.portalRunning) {
        ui.drawPortalScreen("NBA-Display-Setup", "192.168.4.1");
        return;
    }

    if (!nba.lock(pdMS_TO_TICKS(50))) {
        // If locked for writing by background task, skip this render frame to avoid stutter
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

    nba.unlock();
}

void nbaBackgroundUpdateTask(void* pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(500)); // Allow setup and display init to complete cleanly
    Serial.println("Background NBA Task started on Core 0");
    unsigned long lastUpdateAttempt = 0;
    unsigned long lastWiFiReconnectAttempt = 0;

    while (true) {
        if (wifiMgr.portalRunning) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        unsigned long now = millis();

        // Auto-reconnect with 15s backoff if dropped (prevents aggressive radio scanning)
        if (WiFi.status() != WL_CONNECTED) {
            if (now - lastWiFiReconnectAttempt >= 15000) {
                lastWiFiReconnectAttempt = now;
                Serial.println("WiFi dropped. Attempting background auto-reconnect...");
                wifiMgr.autoConnect();
            }
        }

        // Auto-sync NTP if not yet synced (e.g. boot without internet or reconnected)
        if (WiFi.status() == WL_CONNECTED && time(nullptr) < 100000) {
            Serial.println("WiFi connected, syncing NTP clock...");
            configTime(TIMEZONE_OFFSET_HOURS * 3600, 0, "pool.ntp.org", "time.google.com");
        }

        bool force = requestForceUpdate;
        unsigned long interval = nba.hasLiveGame() ? UPDATE_INTERVAL_LIVE : UPDATE_INTERVAL_NORMAL;

        if (WiFi.status() == WL_CONNECTED && (force || (now - lastUpdateAttempt >= interval))) {
            requestForceUpdate = false;
            lastUpdateAttempt = now;

            Serial.println("Background updating NBA data...");
            bool wasFavLive = nba.hasFavoriteLiveGame();
            nba.updateData(force);
            bool isFavLive = nba.hasFavoriteLiveGame();

            // Auto-switch to Live Mode when favorite team game just started!
            if (!wasFavLive && isFavLive && !userNavigatedManually) {
                Serial.println("Favorite team is live! Auto-switching to Live Scoreboard.");
                currentMode = 1;
            } 
            // Auto-return to Standby Clock only when favorite team game has genuinely finished (state == "post")
            else if (wasFavLive && !isFavLive && !userNavigatedManually && currentMode == 1) {
                nba.lock();
                bool genuinelyFinished = (nba.favoriteGame.isValid && nba.favoriteGame.state == "post");
                nba.unlock();
                if (genuinelyFinished) {
                    Serial.println("Favorite game finished. Auto-returning to Standby Clock.");
                    currentMode = 0;
                } else {
                    Serial.println("Favorite game not marked finished yet; keeping scoreboard active.");
                }
            }

            needsDisplayRefresh = true;
        }

        // Sleep in small increments to be responsive to force refresh
        for (int i = 0; i < 20; i++) {
            if (requestForceUpdate || wifiMgr.portalRunning) break;
            vTaskDelay(pdMS_TO_TICKS(50));
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
    nba.initMutex();

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
        nba.lock();
        nba.lastStatusMsg = "WiFi OK";
        nba.unlock();

        // Sync real-time clock via NTP (Thailand UTC+7)
        configTime(TIMEZONE_OFFSET_HOURS * 3600, 0, "pool.ntp.org", "time.google.com");

        currentMode = 0; // Default to Standby Clock immediately
    } else {
        Serial.println("No saved Wi-Fi found. Starting Setup Portal...");
        wifiMgr.startPortal();
    }

    // Switch display away from "Connecting to Wi-Fi..." immediately!
    refreshDisplay();

    // Start background network task on Core 0 to perform initial fetch and future updates
    requestForceUpdate = true;
    xTaskCreatePinnedToCore(
        nbaBackgroundUpdateTask,
        "nba_update",
        16384,
        NULL,
        1,
        &nbaTaskHandle,
        0
    );
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
            userNavigatedManually = true;

            if (currentMode == 0) {
                // Clock -> GSW Match Detail
                currentMode = 1;
            } else if (currentMode == 1) {
                // GSW Match Detail -> All Games
                currentMode = 2;
                allGamesPageIndex = 0;
            } else if (currentMode == 2) {
                nba.lock();
                int totalGames = nba.allGames.size();
                nba.unlock();

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
                nba.lock();
                bool hasConfs = !nba.conferences.empty();
                size_t confCount = nba.conferences.size();
                size_t entryCount = hasConfs ? nba.conferences[standingsConfIndex].entries.size() : 0;
                nba.unlock();

                if (hasConfs) {
                    int totalPages = (entryCount + 3) / 4;
                    if (totalPages == 0) totalPages = 1;

                    standingsPageIndex++;
                    if (standingsPageIndex >= totalPages) {
                        standingsPageIndex = 0;
                        standingsConfIndex++;
                        if (standingsConfIndex >= (int)confCount) {
                            // Standings done -> Back to Standby Clock (or Live if match active)
                            currentMode = nba.hasFavoriteLiveGame() ? 1 : 0;
                            userNavigatedManually = false;
                        }
                    }
                } else {
                    currentMode = nba.hasFavoriteLiveGame() ? 1 : 0;
                    userNavigatedManually = false;
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
            Serial.println("BTN1 short press: Trigger background force refresh");
            nba.lock();
            nba.lastStatusMsg = "Updating...";
            nba.unlock();
            requestForceUpdate = true;
            refreshDisplay();
        }
    }

    // If background task updated data, refresh screen
    if (needsDisplayRefresh) {
        needsDisplayRefresh = false;
        refreshDisplay();
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

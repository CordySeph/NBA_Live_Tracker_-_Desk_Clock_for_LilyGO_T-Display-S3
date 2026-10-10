#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>
#include <time.h>
#include "config.h"
#include "nba_data.h"

// ST7789 Initialization commands for LilyGO T-Display-S3
typedef struct {
    uint8_t cmd;
    uint8_t data[14];
    uint8_t len;
} lcd_cmd_t;

static const lcd_cmd_t lcd_st7789v[] = {
    {0x11, {0}, 0 | 0x80},
    {0x3A, {0X05}, 1},
    {0xB2, {0X0B, 0X0B, 0X00, 0X33, 0X33}, 5},
    {0xB7, {0X75}, 1},
    {0xBB, {0X28}, 1},
    {0xC0, {0X2C}, 1},
    {0xC2, {0X01}, 1},
    {0xC3, {0X1F}, 1},
    {0xC6, {0X13}, 1},
    {0xD0, {0XA7}, 1},
    {0xD0, {0XA4, 0XA1}, 2},
    {0xD6, {0XA1}, 1},
    {0xE0, {0XF0, 0X05, 0X0A, 0X06, 0X06, 0X03, 0X2B, 0X32, 0X43, 0X36, 0X11, 0X10, 0X2B, 0X32}, 14},
    {0xE1, {0XF0, 0X08, 0X0C, 0X0B, 0X09, 0X24, 0X2B, 0X22, 0X43, 0X38, 0X15, 0X16, 0X2F, 0X37}, 14},
};

// 16-bit RGB565 Colors
#define C_BG          0x08A4  // Deep Slate / Navy
#define C_CARD_BG     0x1908  // Dark Card Background
#define C_CARD_BORDER 0x31C9  // Subtle Border
#define C_GSW_BLUE    0x1A11  // GSW Royal Blue (#1D428A)
#define C_GSW_GOLD    0xFE25  // GSW Gold (#FFC72C)
#define C_WHITE       0xFFFF
#define C_LIGHT_GRAY  0xCE79
#define C_GRAY        0x8C71
#define C_DARK_GRAY   0x31A6
#define C_LIVE_RED    0xE945  // Vibrant Live Red
#define C_LIVE_GREEN  0x2E6E  // Vibrant Live Green
#define C_FINAL_GRAY  0x52AA  // Final Badge Gray

class UIDisplay {
public:
    TFT_eSPI tft = TFT_eSPI();
    TFT_eSprite spr = TFT_eSprite(&tft);

    void init() {
        // Power on display logic and backlight
        pinMode(PIN_POWER_ON, OUTPUT);
        digitalWrite(PIN_POWER_ON, HIGH);

        tft.begin();

        // Apply ST7789 commands for LilyGO panel
        for (uint8_t i = 0; i < (sizeof(lcd_st7789v) / sizeof(lcd_cmd_t)); i++) {
            tft.writecommand(lcd_st7789v[i].cmd);
            for (int j = 0; j < (lcd_st7789v[i].len & 0x7f); j++) {
                tft.writedata(lcd_st7789v[i].data[j]);
            }
            if (lcd_st7789v[i].len & 0x80) {
                delay(120);
            }
        }

        tft.setRotation(3); // Landscape (320 x 170)
        tft.fillScreen(TFT_BLACK);

        // Turn on Backlight
        pinMode(PIN_LCD_BL, OUTPUT);
        digitalWrite(PIN_LCD_BL, HIGH);

        // Create double-buffer Sprite (320x170)
        spr.createSprite(320, 170);
        spr.setTextDatum(TL_DATUM);
    }

    void drawConnectingWiFi(const String& ssid) {
        spr.fillSprite(C_BG);

        // Title Box
        spr.fillRoundRect(20, 20, 280, 130, 8, C_CARD_BG);
        spr.drawRoundRect(20, 20, 280, 130, 8, C_GSW_GOLD);

        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(C_GSW_GOLD, C_CARD_BG);
        spr.drawString("NBA SCOREBOARD", 160, 45, 4);

        spr.setTextColor(C_WHITE, C_CARD_BG);
        spr.drawString("Connecting to Wi-Fi...", 160, 78, 2);

        spr.setTextColor(C_LIGHT_GRAY, C_CARD_BG);
        spr.drawString(ssid, 160, 102, 2);

        spr.setTextColor(C_GRAY, C_CARD_BG);
        spr.drawString("Please wait...", 160, 125, 2);

        spr.pushSprite(0, 0);
    }

    void drawPortalScreen(const String& apName, const String& ip) {
        spr.fillSprite(C_BG);

        // Header
        spr.fillRect(0, 0, 320, 24, C_GSW_BLUE);
        spr.drawFastHLine(0, 24, 320, C_GSW_GOLD);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(C_GSW_GOLD, C_GSW_BLUE);
        spr.drawString("WI-FI SETUP MODE", 160, 12, 2);

        // Instructions Card
        spr.fillRoundRect(12, 32, 296, 128, 6, C_CARD_BG);
        spr.drawRoundRect(12, 32, 296, 128, 6, C_GSW_GOLD);

        spr.setTextDatum(ML_DATUM);
        spr.setTextColor(C_WHITE, C_CARD_BG);
        spr.drawString("1. Connect your phone to Wi-Fi:", 24, 50, 2);

        spr.setTextColor(C_GSW_GOLD, C_CARD_BG);
        spr.drawString("SSID: " + apName, 40, 72, 4);

        spr.setTextColor(C_WHITE, C_CARD_BG);
        spr.drawString("2. Open browser / setup page:", 24, 98, 2);

        spr.setTextColor(C_LIVE_GREEN, C_CARD_BG);
        spr.drawString("http://" + ip, 40, 118, 4);

        spr.setTextColor(C_GRAY, C_CARD_BG);
        spr.drawString("Choose network & tap Save", 24, 142, 2);

        spr.pushSprite(0, 0);
    }

    // ========================================================
    // Standby Mode: Desk Clock + Next Match Countdown + Recap
    // ========================================================
    void drawStandbyClockPage(NBAClient& nba) {
        spr.fillSprite(C_BG);

        // Get current local time (10ms timeout so it never blocks UI)
        struct tm timeinfo;
        bool hasTime = getLocalTime(&timeinfo, 10);

        char timeHM[10] = "--:--";
        char timeSec[6] = ":--";
        char dateStr[32] = "NBA Standby";

        if (hasTime) {
            snprintf(timeHM, sizeof(timeHM), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
            snprintf(timeSec, sizeof(timeSec), ":%02d", timeinfo.tm_sec);
            strftime(dateStr, sizeof(dateStr), "%a, %d %b", &timeinfo);
        }

        // ----------------------------------------------------
        // Left Panel: Big Digital Clock & Rank
        // ----------------------------------------------------
        spr.setTextDatum(ML_DATUM);
        spr.setTextColor(C_GSW_GOLD, C_BG);
        spr.drawString("DUBNATION", 10, 16, 2);

        // Huge Hour:Minute (Font 7, width 140px, completely unobstructed)
        spr.setTextDatum(TL_DATUM);
        spr.setTextColor(C_WHITE, C_BG);
        spr.drawString(timeHM, 6, 32, 7);

        // Date String (Left aligned)
        spr.setTextDatum(ML_DATUM);
        spr.setTextColor(C_LIGHT_GRAY, C_BG);
        spr.drawString(dateStr, 8, 96, 2);

        // Live Seconds Counter (Right aligned beside date in Gold, no overlap)
        spr.setTextDatum(MR_DATUM);
        spr.setTextColor(C_GSW_GOLD, C_BG);
        spr.drawString(timeSec, 146, 96, 4);

        // Standings Badge (e.g. #5 WEST • 1-1)
        spr.fillRoundRect(8, 115, 140, 26, 4, C_CARD_BG);
        spr.drawRoundRect(8, 115, 140, 26, 4, C_GSW_BLUE);

        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(C_GSW_GOLD, C_CARD_BG);
        String rankStr = (nba.favoriteRank > 0) ? ("#" + String(nba.favoriteRank) + " " + nba.favoriteConference) : "WARRIORS";
        if (nba.lastGame.isValid && nba.lastGame.away.record.length() > 0) {
            rankStr += " • " + (nba.lastGame.away.abbreviation == "GS" ? nba.lastGame.away.record : nba.lastGame.home.record);
        }
        spr.drawString(rankStr, 79, 128, 2);

        // ----------------------------------------------------
        // Right Panel: Next Game Countdown & Last Game Recap
        // ----------------------------------------------------
        spr.fillRoundRect(156, 8, 156, 135, 6, C_CARD_BG);
        spr.drawRoundRect(156, 8, 156, 135, 6, C_CARD_BORDER);

        // Next Match Header
        spr.setTextDatum(ML_DATUM);
        spr.setTextColor(C_GSW_GOLD, C_CARD_BG);
        spr.drawString("NEXT MATCH", 164, 20, 2);

        // Countdown Badge
        String countdown = nba.getCountdownString();
        spr.fillRoundRect(236, 12, 70, 16, 3, C_GSW_BLUE);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(C_WHITE, C_GSW_BLUE);
        spr.drawString(countdown, 271, 20, 2);

        // Next Matchup: e.g. GSW vs SAC
        spr.setTextDatum(ML_DATUM);
        spr.setTextColor(C_WHITE, C_CARD_BG);
        String matchup = "GSW vs SAC";
        if (nba.nextGame.isValid) {
            matchup = nba.nextGame.away.abbreviation + " @ " + nba.nextGame.home.abbreviation;
        }
        spr.drawString(matchup, 164, 42, 4);

        // Match local date & time
        spr.setTextColor(C_LIGHT_GRAY, C_CARD_BG);
        String matchDate = (nba.nextGameFormatted.length() > 0) ? nba.nextGameFormatted : "TBD";
        spr.drawString(matchDate, 164, 64, 2);

        // Separator Line
        spr.drawFastHLine(162, 78, 144, C_CARD_BORDER);

        // Last Game Recap
        spr.setTextColor(C_GRAY, C_CARD_BG);
        spr.drawString("LAST GAME:", 164, 90, 2);

        if (nba.lastGame.isValid) {
            spr.setTextColor(C_WHITE, C_CARD_BG);
            String recap = nba.lastGame.away.abbreviation + " " + nba.lastGame.away.score + " - " + 
                           nba.lastGame.home.score + " " + nba.lastGame.home.abbreviation;
            spr.drawString(recap, 164, 108, 2);

            spr.setTextColor(C_FINAL_GRAY, C_CARD_BG);
            spr.drawString("FINAL", 164, 124, 2);
        } else {
            spr.setTextColor(C_GRAY, C_CARD_BG);
            spr.drawString("No recent game", 164, 108, 2);
        }

        // ----------------------------------------------------
        // Bottom Footer
        // ----------------------------------------------------
        spr.drawFastHLine(0, 148, 320, C_CARD_BORDER);
        spr.setTextDatum(ML_DATUM);
        spr.setTextColor(C_GRAY, C_BG);
        spr.drawString("Standby Clock", 8, 159, 2);

        spr.setTextDatum(MR_DATUM);
        spr.setTextColor(C_GSW_GOLD, C_BG);
        spr.drawString("[BTN2] Live / Scores >", 312, 159, 2);

        spr.pushSprite(0, 0);
    }

    void drawGSWPage(const GameData& game, int favRank, const String& favConf, const String& statusMsg, unsigned long lastFetch) {
        spr.fillSprite(C_BG);

        // Top Header Bar
        spr.fillRect(0, 0, 320, 24, C_GSW_BLUE);
        spr.drawFastHLine(0, 24, 320, C_GSW_GOLD);

        spr.setTextDatum(ML_DATUM);
        spr.setTextColor(C_GSW_GOLD, C_GSW_BLUE);
        String headerTitle = "WARRIORS";
        if (favRank > 0) {
            headerTitle += " (#" + String(favRank) + " " + favConf + ")";
        }
        spr.drawString(headerTitle, 8, 12, 2);

        // Status Badge on Top Right
        drawStatusBadge(195, 3, 120, 18, game);

        // Main Match Area
        if (!game.isValid) {
            spr.setTextDatum(MC_DATUM);
            if (WiFi.status() != WL_CONNECTED) {
                spr.setTextColor(C_LIVE_RED, C_BG);
                spr.drawString("WiFi Not Connected!", 160, 55, 4);
                spr.setTextColor(C_WHITE, C_BG);
                spr.drawString("SSID: " + String(WIFI_SSID), 160, 85, 2);
                spr.setTextColor(C_GRAY, C_BG);
                spr.drawString("Connecting to network...", 160, 110, 2);
            } else {
                spr.setTextColor(C_WHITE, C_BG);
                spr.drawString("No GSW match data found", 160, 60, 4);
                spr.setTextColor(C_LIGHT_GRAY, C_BG);
                spr.drawString("Status: " + statusMsg, 160, 90, 2);
                spr.setTextColor(C_GSW_GOLD, C_BG);
                spr.drawString("Press BTN1 to retry", 160, 115, 2);
            }
        } else {
            // Away Team Card
            bool awayIsFav = (game.away.abbreviation == "GS" || game.away.abbreviation == "GSW");
            drawTeamCard(8, 28, 146, 112, game.away, awayIsFav);

            // Home Team Card
            bool homeIsFav = (game.home.abbreviation == "GS" || game.home.abbreviation == "GSW");
            drawTeamCard(166, 28, 146, 112, game.home, homeIsFav);

            // Center "@"
            spr.setTextDatum(MC_DATUM);
            spr.setTextColor(C_GSW_GOLD, C_BG);
            spr.drawString("@", 160, 50, 4);

            // Center Quarter/Clock indicator
            spr.setTextColor(C_WHITE, C_BG);
            spr.drawString(game.statusDetail, 160, 125, 2);
        }

        // Footer Bar
        drawFooter(0, statusMsg, lastFetch);

        spr.pushSprite(0, 0);
    }

    void drawAllGamesPage(const std::vector<GameData>& games, int pageIdx, int totalPages, const String& statusMsg, unsigned long lastFetch) {
        spr.fillSprite(C_BG);

        // Top Header Bar
        spr.fillRect(0, 0, 320, 24, C_CARD_BG);
        spr.drawFastHLine(0, 24, 320, C_CARD_BORDER);

        spr.setTextDatum(ML_DATUM);
        spr.setTextColor(C_WHITE, C_CARD_BG);
        String headerTitle = "NBA TODAY (" + String(games.size()) + " Games)";
        spr.drawString(headerTitle, 8, 12, 2);

        spr.setTextDatum(MR_DATUM);
        spr.setTextColor(C_GSW_GOLD, C_CARD_BG);
        String pageStr = "Page " + String(pageIdx + 1) + "/" + String(totalPages > 0 ? totalPages : 1);
        spr.drawString(pageStr, 312, 12, 2);

        // Display up to 3 matches
        int startIdx = pageIdx * 3;
        for (int i = 0; i < 3; i++) {
            int gameIdx = startIdx + i;
            int yPos = 28 + (i * 38);

            if (gameIdx < (int)games.size()) {
                const GameData& g = games[gameIdx];
                bool isGS = g.isFavorite;

                // Card background
                spr.fillRoundRect(6, yPos, 308, 35, 4, C_CARD_BG);
                spr.drawRoundRect(6, yPos, 308, 35, 4, isGS ? C_GSW_GOLD : C_CARD_BORDER);

                // Teams and Scores
                spr.setTextDatum(ML_DATUM);
                spr.setTextColor(isGS && (g.away.abbreviation == "GS" || g.away.abbreviation == "GSW") ? C_GSW_GOLD : C_WHITE, C_CARD_BG);
                spr.drawString(g.away.abbreviation + " " + g.away.score, 12, yPos + 17, 2);

                spr.setTextColor(C_GRAY, C_CARD_BG);
                spr.drawString("@", 88, yPos + 17, 2);

                spr.setTextColor(isGS && (g.home.abbreviation == "GS" || g.home.abbreviation == "GSW") ? C_GSW_GOLD : C_WHITE, C_CARD_BG);
                spr.drawString(g.home.abbreviation + " " + g.home.score, 106, yPos + 17, 2);

                // Game status badge
                drawStatusBadge(195, yPos + 7, 115, 21, g);
            }
        }

        if (games.empty()) {
            spr.setTextDatum(MC_DATUM);
            if (WiFi.status() != WL_CONNECTED) {
                spr.setTextColor(C_LIVE_RED, C_BG);
                spr.drawString("WiFi Not Connected!", 160, 75, 4);
                spr.setTextColor(C_GRAY, C_BG);
                spr.drawString("Please check WiFi credentials", 160, 105, 2);
            } else {
                spr.setTextColor(C_GRAY, C_BG);
                spr.drawString("No games scheduled today", 160, 85, 2);
            }
        }

        // Footer Bar
        drawFooter(1, statusMsg, lastFetch);

        spr.pushSprite(0, 0);
    }

    void drawStandingsPage(const ConferenceStandings& conf, int pageIdx, int totalPages, const String& statusMsg, unsigned long lastFetch) {
        spr.fillSprite(C_BG);

        // Header Bar
        spr.fillRect(0, 0, 320, 24, C_CARD_BG);
        spr.drawFastHLine(0, 24, 320, C_CARD_BORDER);

        spr.setTextDatum(ML_DATUM);
        spr.setTextColor(C_WHITE, C_CARD_BG);
        String title = (conf.name.indexOf("West") >= 0) ? "WEST STANDINGS" : "EAST STANDINGS";
        spr.drawString(title, 8, 12, 2);

        spr.setTextDatum(MR_DATUM);
        spr.setTextColor(C_GSW_GOLD, C_CARD_BG);
        spr.drawString("Page " + String(pageIdx + 1) + "/" + String(totalPages), 312, 12, 2);

        // Table Header
        spr.setTextDatum(ML_DATUM);
        spr.setTextColor(C_GRAY, C_BG);
        spr.drawString("RK", 10, 34, 2);
        spr.drawString("TEAM", 44, 34, 2);
        spr.drawString("W-L", 205, 34, 2);
        spr.drawString("GB", 270, 34, 2);

        // Entries (up to 4 rows per page)
        int startIdx = pageIdx * 4;
        for (int i = 0; i < 4; i++) {
            int idx = startIdx + i;
            if (idx >= (int)conf.entries.size()) break;

            const StandingEntry& entry = conf.entries[idx];
            int yPos = 46 + (i * 24);

            if (entry.isFavorite) {
                spr.fillRoundRect(6, yPos - 1, 308, 22, 3, C_CARD_BG);
                spr.drawRoundRect(6, yPos - 1, 308, 22, 3, C_GSW_GOLD);
            }

            uint16_t txtColor = entry.isFavorite ? C_GSW_GOLD : C_WHITE;
            spr.setTextColor(txtColor, entry.isFavorite ? C_CARD_BG : C_BG);

            // Rank
            spr.drawString(String(entry.rank), 12, yPos + 10, 2);

            // Team abbreviation & short name
            String teamText = entry.abbreviation + " " + entry.displayName;
            if (teamText.length() > 14) teamText = teamText.substring(0, 14);
            spr.drawString(teamText, 44, yPos + 10, 2);

            // W-L
            spr.drawString(entry.record, 205, yPos + 10, 2);

            // GB
            spr.drawString(entry.gb, 270, yPos + 10, 2);
        }

        // Footer Bar
        drawFooter(2, statusMsg, lastFetch);

        spr.pushSprite(0, 0);
    }

private:
    void drawTeamCard(int x, int y, int w, int h, const TeamInfo& t, bool isFavorite) {
        spr.fillRoundRect(x, y, w, h, 6, C_CARD_BG);
        spr.drawRoundRect(x, y, w, h, 6, isFavorite ? C_GSW_GOLD : C_CARD_BORDER);

        if (isFavorite) {
            spr.fillRect(x + 10, y + 2, w - 20, 2, C_GSW_GOLD);
        }

        // Team Abbreviation
        spr.setTextDatum(ML_DATUM);
        spr.setTextColor(isFavorite ? C_GSW_GOLD : C_WHITE, C_CARD_BG);
        spr.drawString(t.abbreviation, x + 10, y + 16, 4);

        // Display Name & Record
        spr.setTextColor(C_GRAY, C_CARD_BG);
        String nameStr = t.displayName;
        if (nameStr.length() > 9) nameStr = nameStr.substring(0, 9);
        spr.drawString(nameStr, x + 10, y + 36, 2);

        if (t.record.length() > 0) {
            spr.drawString("(" + t.record + ")", x + 10, y + 50, 2);
        }

        // Score (Large)
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(isFavorite ? C_GSW_GOLD : C_WHITE, C_CARD_BG);
        int fontToUse = (t.score.length() > 0 && isDigit(t.score[0])) ? 6 : 4;
        spr.drawString(t.score, x + (w / 2), y + 80, fontToUse);
    }

    void drawStatusBadge(int x, int y, int w, int h, const GameData& g) {
        uint16_t badgeColor = C_FINAL_GRAY;
        String badgeText = g.statusDetail;

        if (g.state == "in") {
            badgeColor = C_LIVE_RED;
            badgeText = "LIVE " + g.statusDetail;
        } else if (g.state == "pre") {
            badgeColor = C_GSW_BLUE;
        } else if (g.state == "post") {
            badgeColor = C_DARK_GRAY;
            badgeText = "FINAL";
        }

        spr.fillRoundRect(x, y, w, h, 4, badgeColor);
        spr.setTextDatum(MC_DATUM);
        spr.setTextColor(C_WHITE, badgeColor);

        if (badgeText.length() > 16) {
            badgeText = badgeText.substring(0, 16);
        }
        spr.drawString(badgeText, x + (w / 2), y + (h / 2), 2);
    }

    void drawFooter(int currentPage, const String& statusMsg, unsigned long lastFetch) {
        spr.drawFastHLine(0, 145, 320, C_CARD_BORDER);

        // Left info: Status / Last update
        spr.setTextDatum(ML_DATUM);
        spr.setTextColor(C_GRAY, C_BG);

        String info;
        if (WiFi.status() == WL_CONNECTED) {
            info = statusMsg;
            if (lastFetch > 0) {
                unsigned long secAgo = (millis() - lastFetch) / 1000;
                info += " (" + String(secAgo) + "s)";
            }
        } else {
            info = "WiFi: Disconnected";
        }
        spr.drawString(info, 8, 157, 2);

        // Right button helper
        spr.setTextDatum(MR_DATUM);
        spr.setTextColor(C_GSW_GOLD, C_BG);
        if (currentPage == 0) {
            spr.drawString("[BTN2] All Games >", 312, 157, 2);
        } else if (currentPage == 1) {
            spr.drawString("[BTN2] Rankings >", 312, 157, 2);
        } else {
            spr.drawString("[BTN2] Next >", 312, 157, 2);
        }
    }
};

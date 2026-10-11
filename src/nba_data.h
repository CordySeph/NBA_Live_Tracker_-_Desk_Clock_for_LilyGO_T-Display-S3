#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <time.h>
#include "config.h"

// Custom Stream that decodes HTTP chunks directly into PSRAM (or heap)
class PsramStream : public Stream {
public:
    char* buffer = nullptr;
    size_t capacity = 0;
    size_t length = 0;
    size_t readPos = 0;

    PsramStream(size_t cap = 262144) {
        capacity = cap;
        if (psramFound()) {
            buffer = (char*)ps_malloc(capacity);
        }
        if (!buffer) {
            buffer = (char*)malloc(capacity);
        }
        if (buffer) {
            buffer[0] = '\0';
        }
    }

    ~PsramStream() {
        if (buffer) {
            free(buffer);
            buffer = nullptr;
        }
    }

    size_t write(uint8_t b) override {
        if (buffer && length < capacity - 1) {
            buffer[length++] = (char)b;
            buffer[length] = '\0';
            return 1;
        }
        return 0;
    }

    size_t write(const uint8_t* data, size_t size) override {
        if (!buffer) return 0;
        size_t toWrite = min(size, capacity - 1 - length);
        if (toWrite > 0) {
            memcpy(buffer + length, data, toWrite);
            length += toWrite;
            buffer[length] = '\0';
        }
        return toWrite;
    }

    int available() override { return (length > readPos) ? (length - readPos) : 0; }
    int read() override { return available() ? buffer[readPos++] : -1; }
    int peek() override { return available() ? buffer[readPos] : -1; }
    void flush() override {}
};


struct TeamInfo {
    String abbreviation;
    String displayName;
    String score;
    String record;
    bool isFavorite = false;
};

struct GameData {
    String id;
    TeamInfo away;
    TeamInfo home;
    String statusDetail; // e.g. "Final", "Halftime", "9:20 - 2nd", "7:00 PM"
    String state;        // "pre", "in", "post"
    bool isFavorite;
    bool isValid;
};

struct StandingEntry {
    int rank;
    String abbreviation;
    String displayName;
    String record;
    String gb;
    bool isFavorite;
};

struct ConferenceStandings {
    String name;
    std::vector<StandingEntry> entries;
};

class NBAClient {
public:
    SemaphoreHandle_t mutex = nullptr;

    std::vector<GameData> allGames;
    GameData favoriteGame;
    GameData lastGame;
    GameData nextGame;
    time_t nextGameEpoch = 0;
    String nextGameFormatted = "";

    std::vector<ConferenceStandings> conferences;
    int favoriteRank = 0;
    String favoriteConference = "West";
    String favTeamOverride = "";

    unsigned long lastFetchTime = 0;
    unsigned long lastStandingsFetch = 0;
    String lastStatusMsg = "Init";

    NBAClient() {
        favoriteGame.isValid = false;
        lastGame.isValid = false;
        nextGame.isValid = false;
    }

    void initMutex() {
        if (!mutex) {
            mutex = xSemaphoreCreateRecursiveMutex();
        }
    }

    bool lock(TickType_t timeout = portMAX_DELAY) {
        if (mutex) return (xSemaphoreTakeRecursive(mutex, timeout) == pdTRUE);
        return true;
    }

    void unlock() {
        if (mutex) xSemaphoreGiveRecursive(mutex);
    }

    void setFavoriteTeam(const String& team) {
        favTeamOverride = team;
    }

    bool isFavoriteTeam(const String& abbr) {
        String fav = (favTeamOverride.length() > 0) ? favTeamOverride : FAVORITE_TEAM;
        fav.toUpperCase();
        String a = abbr;
        a.toUpperCase();

        if (a == fav) return true;
        if ((fav == "GS" || fav == "GSW") && (a == "GS" || a == "GSW")) return true;
        return false;
    }

    bool updateData(bool forceStandings = false) {
        if (WiFi.status() != WL_CONNECTED) {
            lock();
            lastStatusMsg = "WiFi not connected";
            unlock();
            return false;
        }

        lock();
        lastStatusMsg = "Fetching...";
        unlock();

        bool ok = fetchScoreboard();

        // Fetch team schedule to get next game countdown and last game recap
        fetchFavoriteSchedule();

        // Fetch standings if forced, or 15 mins passed, or empty but at least 5 mins since last attempt
        unsigned long nowMs = millis();
        bool needStandings = forceStandings || 
                             (conferences.empty() && (nowMs - lastStandingsFetch > 5 * 60 * 1000)) ||
                             (nowMs - lastStandingsFetch > 15 * 60 * 1000);
        if (needStandings) {
            fetchStandings();
        }

        lock();
        if (ok) {
            lastFetchTime = millis();
            lastStatusMsg = "Updated (" + String(allGames.size()) + " games)";
            unlock();
            return true;
        } else {
            unlock();
            return false;
        }
    }

    bool hasFavoriteLiveGame() {
        lock();
        bool live = (favoriteGame.isValid && favoriteGame.state == "in");
        unlock();
        return live;
    }

    bool hasLiveGame() {
        if (hasFavoriteLiveGame()) return true;
        lock();
        bool live = false;
        for (const auto& g : allGames) {
            if (g.state == "in") {
                live = true;
                break;
            }
        }
        unlock();
        return live;
    }

    String getCountdownString() {
        lock();
        time_t targetEpoch = nextGameEpoch;
        String fallbackFormatted = nextGameFormatted;
        unlock();

        if (targetEpoch == 0) return "TBD";
        time_t now = time(nullptr);
        if (now < 100000) return fallbackFormatted; // NTP not synced yet

        if (targetEpoch > now) {
            long diff = targetEpoch - now;
            int days = diff / 86400;
            int hours = (diff % 86400) / 3600;
            int mins = (diff % 3600) / 60;

            if (days > 0) {
                return "In " + String(days) + "d " + String(hours) + "h";
            } else if (hours > 0) {
                return "In " + String(hours) + "h " + String(mins) + "m";
            } else {
                return "In " + String(mins) + "m";
            }
        }
        return "Upcoming";
    }

private:
    time_t parseIso8601(const String& isoStr) {
        int y, m, d, hr, mn;
        if (sscanf(isoStr.c_str(), "%d-%d-%dT%d:%d", &y, &m, &d, &hr, &mn) == 5) {
            int a = (14 - m) / 12;
            int yr = y + 4800 - a;
            int mo = m + 12 * a - 3;
            long jdn = d + (153 * mo + 2) / 5 + 365L * yr + yr / 4 - yr / 100 + yr / 400 - 32045;
            long daysSince1970 = jdn - 2440588;
            return (time_t)(daysSince1970 * 86400L + hr * 3600L + mn * 60L);
        }
        return 0;
    }

    String formatLocalMatchDate(time_t utcEpoch, int tzOffsetHours) {
        if (utcEpoch == 0) return "";
        time_t localEpoch = utcEpoch + (tzOffsetHours * 3600);
        struct tm tmLocal;
        if (!gmtime_r(&localEpoch, &tmLocal)) return "";
        char buf[32];
        strftime(buf, sizeof(buf), "%a %d %b, %H:%M", &tmLocal);
        return String(buf);
    }

    bool fetchScoreboard() {
        WiFiClient client;
        client.setTimeout(6);
        HTTPClient http;
        http.setTimeout(6000);
        http.setReuse(false);
        
        String url = "http://site.api.espn.com/apis/site/v2/sports/basketball/nba/scoreboard";
        http.begin(client, url);

        int httpCode = http.GET();
        if (httpCode != HTTP_CODE_OK) {
            lock();
            lastStatusMsg = "HTTP Err " + String(httpCode);
            unlock();
            http.end();
            return false;
        }

        PsramStream psramStream(131072);
        if (!psramStream.buffer) {
            lock();
            lastStatusMsg = "PSRAM Alloc Fail";
            unlock();
            http.end();
            return false;
        }

        int written = http.writeToStream(&psramStream);
        http.end();

        if (written <= 0 || psramStream.length == 0) {
            lock();
            lastStatusMsg = "Empty response (" + String(written) + ")";
            unlock();
            return false;
        }

        // Set up filter for ArduinoJson 7 in PSRAM
        JsonDocument filter;
        filter["events"][0]["id"] = true;
        filter["events"][0]["status"]["type"]["state"] = true;
        filter["events"][0]["status"]["type"]["shortDetail"] = true;
        filter["events"][0]["competitions"][0]["competitors"][0]["score"] = true;
        filter["events"][0]["competitions"][0]["competitors"][0]["homeAway"] = true;
        filter["events"][0]["competitions"][0]["records"][0]["summary"] = true;
        filter["events"][0]["competitions"][0]["competitors"][0]["team"]["abbreviation"] = true;
        filter["events"][0]["competitions"][0]["competitors"][0]["team"]["shortDisplayName"] = true;

        JsonDocument doc;
        DeserializationError error = deserializeJson(
            doc, 
            psramStream.buffer, 
            DeserializationOption::Filter(filter),
            DeserializationOption::NestingLimit(25)
        );

        if (error) {
            lock();
            lastStatusMsg = "JSON: " + String(error.c_str());
            unlock();
            return false;
        }

        std::vector<GameData> parsedGames;
        GameData parsedFav;
        parsedFav.isValid = false;
        GameData parsedLast;
        parsedLast.isValid = false;

        JsonArray events = doc["events"].as<JsonArray>();
        for (JsonObject ev : events) {
            GameData game;
            game.id = ev["id"].as<String>();
            game.state = ev["status"]["type"]["state"].as<String>();
            game.statusDetail = ev["status"]["type"]["shortDetail"].as<String>();
            game.isValid = true;
            game.isFavorite = false;

            JsonArray competitors = ev["competitions"][0]["competitors"].as<JsonArray>();
            for (JsonObject c : competitors) {
                String homeAway = c["homeAway"].as<String>();
                TeamInfo t;
                t.abbreviation = c["team"]["abbreviation"].as<String>();
                t.displayName = c["team"]["shortDisplayName"].as<String>();
                t.score = !c["score"].isNull() ? c["score"].as<String>() : "0";
                t.record = !c["records"][0]["summary"].isNull() ? c["records"][0]["summary"].as<String>() : "";
                t.isFavorite = isFavoriteTeam(t.abbreviation);

                if (t.isFavorite) {
                    game.isFavorite = true;
                }

                if (homeAway == "home") {
                    game.home = t;
                } else {
                    game.away = t;
                }
            }

            parsedGames.push_back(game);

            if (game.isFavorite) {
                parsedFav = game;
                if (game.state == "post") {
                    parsedLast = game;
                }
            }
        }

        lock();
        allGames = std::move(parsedGames);
        if (parsedFav.isValid) {
            favoriteGame = parsedFav;
        }
        if (parsedLast.isValid) {
            lastGame = parsedLast;
        }
        size_t totalParsed = allGames.size();
        unlock();

        return (totalParsed > 0);
    }

    bool fetchFavoriteSchedule() {
        WiFiClient client;
        client.setTimeout(6);
        HTTPClient http;
        http.setTimeout(6000);
        http.setReuse(false);

        String teamCode = (favTeamOverride.length() > 0) ? favTeamOverride : FAVORITE_TEAM;
        teamCode.toLowerCase();
        if (teamCode == "gsw") teamCode = "gs";
        String url = "http://site.api.espn.com/apis/site/v2/sports/basketball/nba/teams/" + teamCode + "/schedule";
        http.begin(client, url);

        int httpCode = http.GET();
        if (httpCode != HTTP_CODE_OK) {
            http.end();
            return false;
        }

        PsramStream psramStream(131072);
        if (!psramStream.buffer) {
            http.end();
            return false;
        }

        int written = http.writeToStream(&psramStream);
        http.end();

        if (written <= 0 || psramStream.length == 0) return false;

        JsonDocument filter;
        filter["events"][0]["id"] = true;
        filter["events"][0]["name"] = true;
        filter["events"][0]["date"] = true;
        filter["events"][0]["competitions"][0]["status"]["type"]["state"] = true;
        filter["events"][0]["competitions"][0]["status"]["type"]["shortDetail"] = true;
        filter["events"][0]["competitions"][0]["competitors"][0]["homeAway"] = true;
        filter["events"][0]["competitions"][0]["competitors"][0]["score"]["displayValue"] = true;
        filter["events"][0]["competitions"][0]["competitors"][0]["team"]["abbreviation"] = true;
        filter["events"][0]["competitions"][0]["competitors"][0]["team"]["shortDisplayName"] = true;

        JsonDocument doc;
        DeserializationError error = deserializeJson(
            doc, 
            psramStream.buffer, 
            DeserializationOption::Filter(filter),
            DeserializationOption::NestingLimit(25)
        );

        if (error) {
            return false;
        }

        JsonArray events = doc["events"].as<JsonArray>();
        if (events.isNull() || events.size() == 0) return false;

        GameData tempFav;
        tempFav.isValid = false;
        GameData tempLast;
        tempLast.isValid = false;
        GameData tempNext;
        tempNext.isValid = false;
        time_t tempNextEpoch = 0;
        String tempNextFormatted = "";
        bool foundNext = false;

        for (JsonObject ev : events) {
            String state = ev["competitions"][0]["status"]["type"]["state"].as<String>();

            GameData game;
            game.id = ev["id"].as<String>();
            game.state = state;
            game.statusDetail = ev["competitions"][0]["status"]["type"]["shortDetail"].as<String>();
            game.isFavorite = true;
            game.isValid = true;

            JsonArray competitors = ev["competitions"][0]["competitors"].as<JsonArray>();
            for (JsonObject c : competitors) {
                String homeAway = c["homeAway"].as<String>();
                TeamInfo t;
                t.abbreviation = c["team"]["abbreviation"].as<String>();
                t.displayName = c["team"]["shortDisplayName"].as<String>();
                t.score = !c["score"]["displayValue"].isNull() ? c["score"]["displayValue"].as<String>() : "-";
                t.isFavorite = isFavoriteTeam(t.abbreviation);

                if (homeAway == "home") {
                    game.home = t;
                } else {
                    game.away = t;
                }
            }

            if (state == "in") {
                tempFav = game;
            } else if (state == "post") {
                tempLast = game;
            } else if (state == "pre" && !foundNext) {
                tempNext = game;
                String isoDate = ev["date"].as<String>();
                tempNextEpoch = parseIso8601(isoDate);
                tempNextFormatted = formatLocalMatchDate(tempNextEpoch, TIMEZONE_OFFSET_HOURS);
                foundNext = true;
            }
        }

        lock();
        // NEVER overwrite favoriteGame if it's already live or populated by fetchScoreboard
        if (tempFav.isValid && !favoriteGame.isValid) {
            favoriteGame = tempFav;
        }
        if (tempLast.isValid) {
            lastGame = tempLast;
        }
        if (tempNext.isValid) {
            nextGame = tempNext;
            nextGameEpoch = tempNextEpoch;
            nextGameFormatted = tempNextFormatted;
        }
        if (!favoriteGame.isValid) {
            if (lastGame.isValid) favoriteGame = lastGame;
            else if (nextGame.isValid) favoriteGame = nextGame;
        }
        unlock();

        return true;
    }

    bool fetchStandings() {
        lastStandingsFetch = millis(); // Mark attempt time immediately to avoid retry storm!

        WiFiClient client;
        client.setTimeout(8);
        HTTPClient http;
        http.setTimeout(8000);
        http.setReuse(false);
        String url = "http://site.api.espn.com/apis/v2/sports/basketball/nba/standings";
        http.begin(client, url);

        int httpCode = http.GET();
        if (httpCode != HTTP_CODE_OK) {
            http.end();
            return false;
        }

        PsramStream psramStream(262144);
        if (!psramStream.buffer) {
            http.end();
            return false;
        }

        int written = http.writeToStream(&psramStream);
        http.end();

        if (written <= 0 || psramStream.length == 0) return false;

        JsonDocument filter;
        filter["children"][0]["name"] = true;
        filter["children"][0]["standings"]["entries"][0]["team"]["abbreviation"] = true;
        filter["children"][0]["standings"]["entries"][0]["team"]["shortDisplayName"] = true;
        filter["children"][0]["standings"]["entries"][0]["stats"][0]["name"] = true;
        filter["children"][0]["standings"]["entries"][0]["stats"][0]["displayValue"] = true;

        JsonDocument doc;
        DeserializationError error = deserializeJson(
            doc, 
            psramStream.buffer, 
            DeserializationOption::Filter(filter),
            DeserializationOption::NestingLimit(25)
        );

        if (error) {
            return false;
        }

        std::vector<ConferenceStandings> tempConferences;
        int tempFavRank = 0;
        String tempFavConf = "West";

        JsonArray children = doc["children"].as<JsonArray>();
        for (JsonObject conf : children) {
            ConferenceStandings cs;
            cs.name = conf["name"].as<String>();

            int rank = 1;
            JsonArray entries = conf["standings"]["entries"].as<JsonArray>();
            for (JsonObject entry : entries) {
                StandingEntry se;
                se.rank = rank++;
                se.abbreviation = entry["team"]["abbreviation"].as<String>();
                se.displayName = entry["team"]["shortDisplayName"].as<String>();
                se.isFavorite = isFavoriteTeam(se.abbreviation);

                JsonArray stats = entry["stats"].as<JsonArray>();
                for (JsonObject stat : stats) {
                    String sname = stat["name"].as<String>();
                    if (sname == "overall") se.record = stat["displayValue"].as<String>();
                    else if (sname == "gamesBehind") se.gb = stat["displayValue"].as<String>();
                }

                if (se.isFavorite) {
                    tempFavRank = se.rank;
                    tempFavConf = (cs.name.indexOf("West") >= 0) ? "West" : "East";
                }

                cs.entries.push_back(se);
            }

            tempConferences.push_back(cs);
        }

        lock();
        conferences = std::move(tempConferences);
        if (tempFavRank > 0) {
            favoriteRank = tempFavRank;
            favoriteConference = tempFavConf;
        }
        unlock();

        return true;
    }
};

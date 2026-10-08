#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <vector>
#include "config.h"

struct SavedNetwork {
    String ssid;
    String password;
};

class MultiWiFiManager {
public:
    Preferences prefs;
    std::vector<SavedNetwork> savedNetworks;
    String favoriteTeam = FAVORITE_TEAM;

    WebServer server;
    DNSServer dnsServer;
    bool portalRunning = false;

    MultiWiFiManager() : server(80) {}

    void init() {
        prefs.begin("nba_wifi", false);
        loadSavedNetworks();

        // If no networks are stored, add the default one from config.h
        if (savedNetworks.empty() && String(WIFI_SSID).length() > 0 && String(WIFI_SSID) != "YOUR_WIFI_SSID") {
            addNetwork(WIFI_SSID, WIFI_PASSWORD);
        }

        // Load favorite team if customized
        String savedFav = prefs.getString("fav_team", "");
        if (savedFav.length() > 0) {
            favoriteTeam = savedFav;
        } else {
            favoriteTeam = FAVORITE_TEAM;
        }
    }

    void loadSavedNetworks() {
        savedNetworks.clear();
        int count = prefs.getInt("count", 0);
        for (int i = 0; i < count; i++) {
            String sKey = "s_" + String(i);
            String pKey = "p_" + String(i);
            String ssid = prefs.getString(sKey.c_str(), "");
            String pass = prefs.getString(pKey.c_str(), "");
            if (ssid.length() > 0) {
                savedNetworks.push_back({ssid, pass});
            }
        }
    }

    void saveAllNetworks() {
        prefs.putInt("count", (int)savedNetworks.size());
        for (size_t i = 0; i < savedNetworks.size(); i++) {
            String sKey = "s_" + String(i);
            String pKey = "p_" + String(i);
            prefs.putString(sKey.c_str(), savedNetworks[i].ssid);
            prefs.putString(pKey.c_str(), savedNetworks[i].password);
        }
    }

    void addNetwork(const String& ssid, const String& password) {
        // Check if already exists, update password
        for (auto& net : savedNetworks) {
            if (net.ssid == ssid) {
                net.password = password;
                saveAllNetworks();
                return;
            }
        }
        // Limit to 8 networks
        if (savedNetworks.size() >= 8) {
            savedNetworks.erase(savedNetworks.begin()); // remove oldest
        }
        savedNetworks.push_back({ssid, password});
        saveAllNetworks();
    }

    void deleteNetwork(int index) {
        if (index >= 0 && index < (int)savedNetworks.size()) {
            savedNetworks.erase(savedNetworks.begin() + index);
            saveAllNetworks();
        }
    }

    void setFavoriteTeam(const String& team) {
        favoriteTeam = team;
        prefs.putString("fav_team", team);
    }

    // Try connecting to any of the saved networks
    bool autoConnect(std::function<void(const String&)> onProgress = nullptr) {
        if (savedNetworks.empty()) {
            return false;
        }

        WiFi.mode(WIFI_STA);
        WiFi.setSleep(false);
        WiFi.disconnect();
        delay(100);

        if (onProgress) onProgress("Scanning Wi-Fi...");
        int n = WiFi.scanNetworks();
        if (n <= 0) {
            // If scan failed, try connecting to saved networks one by one
            for (const auto& net : savedNetworks) {
                if (onProgress) onProgress("Connecting to " + net.ssid);
                if (tryConnect(net.ssid, net.password)) return true;
            }
            return false;
        }

        // Match scanned networks against our saved networks
        // Priority to strongest signal matching network
        int bestIdx = -1;
        int bestRssi = -1000;
        int savedNetIdx = -1;

        for (int i = 0; i < n; i++) {
            String scannedSSID = WiFi.SSID(i);
            int rssi = WiFi.RSSI(i);

            for (size_t j = 0; j < savedNetworks.size(); j++) {
                if (scannedSSID == savedNetworks[j].ssid) {
                    if (rssi > bestRssi) {
                        bestRssi = rssi;
                        bestIdx = i;
                        savedNetIdx = (int)j;
                    }
                }
            }
        }

        if (savedNetIdx >= 0) {
            const auto& net = savedNetworks[savedNetIdx];
            if (onProgress) onProgress("Found " + net.ssid);
            if (tryConnect(net.ssid, net.password)) {
                return true;
            }
        }

        // Fallback: try remaining saved networks
        for (size_t j = 0; j < savedNetworks.size(); j++) {
            if ((int)j == savedNetIdx) continue;
            if (onProgress) onProgress("Trying " + savedNetworks[j].ssid);
            if (tryConnect(savedNetworks[j].ssid, savedNetworks[j].password)) {
                return true;
            }
        }

        return false;
    }

    bool tryConnect(const String& ssid, const String& password) {
        WiFi.begin(ssid.c_str(), password.c_str());
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
            delay(300);
        }
        return (WiFi.status() == WL_CONNECTED);
    }

    // ========================================================
    // Captive Portal Web Server
    // ========================================================
    void startPortal() {
        WiFi.mode(WIFI_AP_STA);
        IPAddress apIP(192, 168, 4, 1);
        WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
        WiFi.softAP("NBA-Display-Setup");

        dnsServer.start(53, "*", apIP);

        server.on("/", HTTP_GET, [this]() { handleRoot(); });
        server.on("/save", HTTP_POST, [this]() { handleSave(); });
        server.on("/delete", HTTP_GET, [this]() { handleDelete(); });
        server.on("/generate_204", [this]() { handleRoot(); });
        server.on("/hotspot-detect.html", [this]() { handleRoot(); });
        server.onNotFound([this]() { handleRoot(); });

        server.begin();
        portalRunning = true;
    }

    void handlePortal() {
        if (portalRunning) {
            dnsServer.processNextRequest();
            server.handleClient();
        }
    }

    void stopPortal() {
        if (portalRunning) {
            dnsServer.stop();
            server.stop();
            WiFi.softAPdisconnect(true);
            portalRunning = false;
        }
    }

private:
    void handleRoot() {
        int n = WiFi.scanComplete();
        if (n == -2) {
            WiFi.scanNetworks(true); // scan in background
        }

        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                      "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
                      "<title>NBA Display Setup</title>"
                      "<style>"
                      "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;background:#0d1117;color:#e6edf3;padding:16px;margin:0;}"
                      ".card{background:#161b22;border:1px solid #30363d;border-radius:10px;padding:18px;max-width:400px;margin:12px auto;}"
                      "h2{color:#fdb927;margin-top:0;font-size:22px;text-align:center;}"
                      "label{display:block;font-size:13px;color:#8b949e;margin-top:12px;margin-bottom:4px;}"
                      "input,select{width:100%;padding:10px;border-radius:6px;border:1px solid #30363d;background:#0d1117;color:#fff;box-sizing:border-box;font-size:15px;}"
                      "input:focus,select:focus{border-color:#1d428a;outline:none;}"
                      ".btn{display:block;width:100%;background:#1d428a;color:#fff;border:none;padding:12px;border-radius:6px;font-size:16px;font-weight:bold;margin-top:18px;cursor:pointer;}"
                      ".btn:hover{background:#2a5ab8;}"
                      ".net-item{background:#21262d;padding:8px 12px;border-radius:6px;margin-bottom:6px;display:flex;justify-content:space-between;align-items:center;cursor:pointer;}"
                      ".net-item:hover{background:#30363d;}"
                      ".del-btn{color:#f85149;text-decoration:none;font-size:13px;padding:4px 8px;border:1px solid #f85149;border-radius:4px;}"
                      "</style>"
                      "<script>"
                      "function pick(s){document.getElementById('ssid').value=s;document.getElementById('pass').focus();}"
                      "</script></head><body>"
                      "<div class='card'>"
                      "<h2>🏀 NBA Display Setup</h2>"
                      "<p style='font-size:13px;color:#8b949e;text-align:center;'>Configure Wi-Fi and Favorite Team</p>"
                      "<form action='/save' method='POST'>"
                      "<label>Wi-Fi Network (SSID):</label>"
                      "<input type='text' id='ssid' name='ssid' placeholder='Select below or enter name' required>"
                      "<label>Wi-Fi Password:</label>"
                      "<input type='password' id='pass' name='pass' placeholder='Enter password'>"
                      "<label>Favorite Team:</label>"
                      "<select name='fav'>"
                      "<option value='GS'" + String(favoriteTeam == "GS" || favoriteTeam == "GSW" ? " selected" : "") + ">Golden State Warriors (GSW)</option>"
                      "<option value='LAL'" + String(favoriteTeam == "LAL" ? " selected" : "") + ">Los Angeles Lakers (LAL)</option>"
                      "<option value='BOS'" + String(favoriteTeam == "BOS" ? " selected" : "") + ">Boston Celtics (BOS)</option>"
                      "<option value='MIL'" + String(favoriteTeam == "MIL" ? " selected" : "") + ">Milwaukee Bucks (MIL)</option>"
                      "<option value='DEN'" + String(favoriteTeam == "DEN" ? " selected" : "") + ">Denver Nuggets (DEN)</option>"
                      "<option value='DAL'" + String(favoriteTeam == "DAL" ? " selected" : "") + ">Dallas Mavericks (DAL)</option>"
                      "<option value='MIA'" + String(favoriteTeam == "MIA" ? " selected" : "") + ">Miami Heat (MIA)</option>"
                      "<option value='NY'" + String(favoriteTeam == "NY" ? " selected" : "") + ">New York Knicks (NY)</option>"
                      "<option value='PHX'" + String(favoriteTeam == "PHX" ? " selected" : "") + ">Phoenix Suns (PHX)</option>"
                      "<option value='CHI'" + String(favoriteTeam == "CHI" ? " selected" : "") + ">Chicago Bulls (CHI)</option>"
                      "<option value='MIN'" + String(favoriteTeam == "MIN" ? " selected" : "") + ">Minnesota Timberwolves (MIN)</option>"
                      "<option value='OKC'" + String(favoriteTeam == "OKC" ? " selected" : "") + ">Oklahoma City Thunder (OKC)</option>"
                      "<option value='SAC'" + String(favoriteTeam == "SAC" ? " selected" : "") + ">Sacramento Kings (SAC)</option>"
                      "<option value='LAC'" + String(favoriteTeam == "LAC" ? " selected" : "") + ">LA Clippers (LAC)</option>"
                      "<option value='PHI'" + String(favoriteTeam == "PHI" ? " selected" : "") + ">Philadelphia 76ers (PHI)</option>"
                      "<option value='CLE'" + String(favoriteTeam == "CLE" ? " selected" : "") + ">Cleveland Cavaliers (CLE)</option>"
                      "<option value='IND'" + String(favoriteTeam == "IND" ? " selected" : "") + ">Indiana Pacers (IND)</option>"
                      "<option value='ORL'" + String(favoriteTeam == "ORL" ? " selected" : "") + ">Orlando Magic (ORL)</option>"
                      "<option value='MEM'" + String(favoriteTeam == "MEM" ? " selected" : "") + ">Memphis Grizzlies (MEM)</option>"
                      "<option value='HOU'" + String(favoriteTeam == "HOU" ? " selected" : "") + ">Houston Rockets (HOU)</option>"
                      "<option value='SA'" + String(favoriteTeam == "SA" ? " selected" : "") + ">San Antonio Spurs (SA)</option>"
                      "<option value='POR'" + String(favoriteTeam == "POR" ? " selected" : "") + ">Portland Trail Blazers (POR)</option>"
                      "<option value='UTAH'" + String(favoriteTeam == "UTAH" ? " selected" : "") + ">Utah Jazz (UTAH)</option>"
                      "<option value='ATL'" + String(favoriteTeam == "ATL" ? " selected" : "") + ">Atlanta Hawks (ATL)</option>"
                      "<option value='TOR'" + String(favoriteTeam == "TOR" ? " selected" : "") + ">Toronto Raptors (TOR)</option>"
                      "<option value='BKN'" + String(favoriteTeam == "BKN" ? " selected" : "") + ">Brooklyn Nets (BKN)</option>"
                      "<option value='NO'" + String(favoriteTeam == "NO" ? " selected" : "") + ">New Orleans Pelicans (NO)</option>"
                      "<option value='CHA'" + String(favoriteTeam == "CHA" ? " selected" : "") + ">Charlotte Hornets (CHA)</option>"
                      "<option value='DET'" + String(favoriteTeam == "DET" ? " selected" : "") + ">Detroit Pistons (DET)</option>"
                      "<option value='WSH'" + String(favoriteTeam == "WSH" ? " selected" : "") + ">Washington Wizards (WSH)</option>"
                      "</select>"
                      "<button type='submit' class='btn'>Save & Connect</button>"
                      "</form></div>";

        // Detected Wi-Fi networks
        html += "<div class='card'><h3 style='margin-top:0;font-size:16px;'>Detected Networks:</h3>";
        int scanCount = WiFi.scanComplete();
        if (scanCount > 0) {
            for (int i = 0; i < scanCount; i++) {
                String s = WiFi.SSID(i);
                int r = WiFi.RSSI(i);
                html += "<div class='net-item' onclick=\"pick('" + s + "')\"><span>" + s + "</span><span style='font-size:12px;color:#8b949e;'>" + String(r) + " dBm</span></div>";
            }
        } else {
            html += "<p style='color:#8b949e;font-size:13px;'>Scanning... refresh page in a few seconds.</p>";
        }
        html += "</div>";

        // Stored Networks
        html += "<div class='card'><h3 style='margin-top:0;font-size:16px;'>Remembered Networks:</h3>";
        if (savedNetworks.empty()) {
            html += "<p style='color:#8b949e;font-size:13px;'>No networks saved yet.</p>";
        } else {
            for (size_t i = 0; i < savedNetworks.size(); i++) {
                html += "<div class='net-item'><span>" + savedNetworks[i].ssid + "</span>"
                        "<a href='/delete?id=" + String(i) + "' class='del-btn'>Remove</a></div>";
            }
        }
        html += "</div></body></html>";

        server.send(200, "text/html", html);
    }

    void handleSave() {
        String newSsid = server.arg("ssid");
        String newPass = server.arg("pass");
        String newFav = server.arg("fav");

        if (newSsid.length() > 0) {
            addNetwork(newSsid, newPass);
        }
        if (newFav.length() > 0) {
            setFavoriteTeam(newFav);
        }

        String resp = "<!DOCTYPE html><html><head><meta charset='UTF-8'>"
                      "<meta name='viewport' content='width=device-width,initial-scale=1.0'>"
                      "<style>body{background:#0d1117;color:#fff;text-align:center;padding:40px;font-family:sans-serif;}</style></head><body>"
                      "<h2>Saved Successfully!</h2>"
                      "<p>Connecting to <b>" + newSsid + "</b>...</p>"
                      "<p>The display is restarting now.</p>"
                      "</body></html>";
        server.send(200, "text/html", resp);

        delay(1500);
        ESP.restart();
    }

    void handleDelete() {
        int id = server.arg("id").toInt();
        deleteNetwork(id);
        server.sendHeader("Location", "/");
        server.send(303);
    }
};

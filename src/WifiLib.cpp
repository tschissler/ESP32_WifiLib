#include "WifiLib.h"
#include <WiFi.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>

// ============================================================================
// Modus 1: Env-Var-basiert (bestehende Projekte, unveraendert)
// ============================================================================

WifiLib::WifiLib(const String& wifiPasswords)
    : passwords(wifiPasswords), ssid(""), password(""), bssidSet(false),
      _storedCredMode(false), _apModeActive(false),
      _httpServer(nullptr), _dnsServer(nullptr), _scannedNetworkCount(0) {
    memset(bssid, 0, sizeof(bssid));
}

void WifiLib::scanAndSelectNetwork() {
    Serial.println("Scanning for WiFi networks...");
    std::map<String, String> knownWifis;
    parseWifis(knownWifis);

    if (knownWifis.size() == 0) {
        if (passwords.length() == 0) {
            Serial.println("No WIFI_PASSWORDS defined, falling back to NVS/AP mode.");
            _storedCredMode = true;
            _loadFromNVS();
            if (ssid.length() == 0) {
                _startAP("ESP32-Setup");
            }
        } else {
            Serial.println("No known WiFi networks defined, will not connect to Wifi.");
        }
        return;
    }

    int numberOfNetworks = WiFi.scanNetworks();
    Serial.print("Found ");
    Serial.print(numberOfNetworks);
    Serial.println(" networks.");
    for (int i = 0; i < numberOfNetworks; i++) {
        Serial.printf("%s  RSSI:%d dBm  ch:%d  BSSID:%s  enc:%d\n",
            WiFi.SSID(i).c_str(),
            WiFi.RSSI(i),
            WiFi.channel(i),
            WiFi.BSSIDstr(i).c_str(),
            WiFi.encryptionType(i));
        delay(10);
    }
    int maxRSSI = -1000;
    int maxRSSIIndex = -1;
    for (int i = 0; i < numberOfNetworks; i++) {
        if (WiFi.RSSI(i) > maxRSSI && knownWifis.count(WiFi.SSID(i)) > 0) {
            maxRSSI = WiFi.RSSI(i);
            maxRSSIIndex = i;
        }
    }

    if (maxRSSIIndex == -1) {
        Serial.println("No WiFi network found that is contained in the list of known networks.");
        Serial.println("Please check your environment variable 'WIFI_PASSWORDS'.");
        Serial.println("Defined networks are:");
        for (const auto& pair : knownWifis) {
            Serial.print(" - ");
            Serial.println(pair.first);
        }

        ssid = "";
        password = "";
        bssidSet = false;
        return;
    } else {
        ssid = WiFi.SSID(maxRSSIIndex);
        password = knownWifis[ssid];

        // Store the BSSID of the strongest access point
        uint8_t* foundBSSID = WiFi.BSSID(maxRSSIIndex);
        if (foundBSSID != nullptr) {
            memcpy(bssid, foundBSSID, 6);
            bssidSet = true;
            Serial.printf("Strongest known WiFi network is %s with RSSI %d dBm, BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
                ssid.c_str(), maxRSSI, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        } else {
            bssidSet = false;
            Serial.println("Strongest known WiFi network is " + ssid + " with RSSI " + String(maxRSSI) + " dBm");
        }
    }
}

void WifiLib::connect() {
    while (ssid == "" || password == "") {
        if (_apModeActive) {
            handle();
            delay(10);
            continue;
        }
        Serial.println("No WiFi network found, retrying...");
        delay(1000);
        scanAndSelectNetwork();
    }

    Serial.print("Connecting to WiFi ");
    Serial.println(ssid);

    // Connect with specific BSSID if available to ensure connection to the strongest AP
    if (bssidSet) {
        Serial.printf("Connecting to specific access point with BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
            bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        WiFi.begin(ssid.c_str(), password.c_str(), 0, bssid, true);
    } else {
        WiFi.begin(ssid.c_str(), password.c_str());
    }

    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        wl_status_t status = WiFi.status();
        if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL) {
            Serial.println("Could not connect to Wifi " + ssid + " - retrying...");
            startMs = millis();
            if (bssidSet) {
                WiFi.begin(ssid.c_str(), password.c_str(), 0, bssid, true);
            } else {
                WiFi.begin(ssid.c_str(), password.c_str());
            }
        } else if (millis() - startMs > 15000) {
            Serial.println("Connection timeout for " + ssid + " - retrying...");
            startMs = millis();
            if (bssidSet) {
                WiFi.begin(ssid.c_str(), password.c_str(), 0, bssid, true);
            } else {
                WiFi.begin(ssid.c_str(), password.c_str());
            }
        }
    }

    Serial.println("Connected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
}

void WifiLib::parseWifis(std::map<String, String> &knownWifis) {
    knownWifis.clear();
    int start = 0;
    while (start < passwords.length()) {
        int end = passwords.indexOf('|', start);
        if (end == -1) end = passwords.length();
        String entry = passwords.substring(start, end);
        int sep = entry.indexOf(';');
        if (sep == -1 || sep == 0 || sep == entry.length() - 1) {
            Serial.println("Error: Invalid WiFi password format. Each entry must be 'SSID;password'.");
            Serial.println("Offending entry: " + entry);
        } else {
            knownWifis[entry.substring(0, sep)] = entry.substring(sep + 1);
        }
        start = end + 1;
    }
}

String WifiLib::getSSID() const { return ssid; }
String WifiLib::getPassword() const { return password; }
String WifiLib::getLocalIP() const { return WiFi.localIP().toString(); }

String WifiLib::getBSSID() const {
    if (bssidSet) {
        char bssidStr[18];
        sprintf(bssidStr, "%02X:%02X:%02X:%02X:%02X:%02X",
            bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
        return String(bssidStr);
    }
    return "";
}

// ============================================================================
// Modus 2: Gespeicherte NVS-Credentials / AP-Einrichtungsmodus
// ============================================================================

WifiLib::WifiLib()
    : ssid(""), password(""), passwords(""), bssidSet(false),
      _storedCredMode(true), _apModeActive(false),
      _httpServer(nullptr), _dnsServer(nullptr), _scannedNetworkCount(0) {
    memset(bssid, 0, sizeof(bssid));
}

bool WifiLib::connectOrStartAP(const String& apName, int timeoutSekunden) {
    _loadFromNVS();

    if (ssid.length() > 0) {
        Serial.println("WifiLib: Gespeicherte Credentials gefunden, verbinde mit " + ssid);
        WiFi.mode(WIFI_STA);
        WiFi.disconnect(false);  // clear any leftover connection state, keep radio on
        delay(100);

        // Scan for the strongest BSSID of the stored SSID (important for mesh networks)
        uint8_t bestBssid[6] = {0};
        bool bestBssidFound = false;
        int bestRssi = -1000;
        Serial.println("WifiLib: Scanne nach bestem AP fuer " + ssid + "...");
        int networkCount = WiFi.scanNetworks();
        for (int i = 0; i < networkCount; i++) {
            if (WiFi.SSID(i) == ssid && WiFi.RSSI(i) > bestRssi) {
                bestRssi = WiFi.RSSI(i);
                memcpy(bestBssid, WiFi.BSSID(i), 6);
                bestBssidFound = true;
            }
        }
        WiFi.scanDelete();
        if (bestBssidFound) {
            Serial.printf("WifiLib: Staerkster AP: %02X:%02X:%02X:%02X:%02X:%02X (%d dBm)\n",
                bestBssid[0], bestBssid[1], bestBssid[2], bestBssid[3], bestBssid[4], bestBssid[5], bestRssi);
        } else {
            Serial.println("WifiLib: Kein passender AP gefunden, versuche ohne BSSID-Pinning.");
        }

        // Disconnect-Grund fuer Diagnose loggen
        wifi_event_id_t disconnectEventId = WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
            uint8_t reason = info.wifi_sta_disconnected.reason;
            Serial.printf("WifiLib: Verbindung getrennt, Reason %d", reason);
            switch (reason) {
                case 4: Serial.print(" (AP Verbindung abgelaufen, z.B. wegen Inaktivität)"); break;
                case 8: Serial.print(" (Client baut Verbindung ab)"); break;
                case 14: Serial.print(" (Authentifizierung fehlgeschlagen - MIC-Fehler, ggf. falsches Passwort)"); break;
                case 15: Serial.print(" (4-Way-Handshake-Timeout – Frame-Verlust: schwaches Signal/Mesh-Knoten oder andere Probleme beim SAE)"); break;
                case 34: Serial.print(" (Zu viele Frames unbestätigt wegen schlechter Kanalbedingungen)"); break;
                case 36: Serial.print(" (Client verlässt die Session)"); break;
                case 200: Serial.print(" (Beacon-Timeout – AP ausser Reichweite?)"); break;
                case 201: Serial.print(" (SSID/AP nicht gefunden – evtl. gepinnter BSSID nicht erreichbar)"); break;
                case 202: Serial.print(" (Auth fehlgeschlagen – Frame-Verlust am Rand der Reichweite moeglich)"); break;
                case 203: Serial.print(" (Assoziierungs-Phase gescheitert)"); break;
                case 204: Serial.print(" (Handshake-Timeout – Frame-Verlust: schwaches Signal/Mesh-Knoten oder SAE)"); break;
                case 205: Serial.print(" (Verbindungsfehler)"); break;
                case 210: Serial.print(" (Kein AP mit kompatibler Verschlüsselung)"); break;
                case 211: Serial.print(" (Kein AP erfüllt die Mindest-Verschlüsselungsart / authmode-Schwelle)"); break;
                case 212: Serial.print(" (Zu schwaches Signal für RSSI-Schwelle)"); break;
                default: Serial.print(" (unbekannter Grund)"); break;
            }
            Serial.println();
        }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

        if (bestBssidFound) {
            WiFi.begin(ssid.c_str(), password.c_str(), 0, bestBssid, true);
        } else {
            WiFi.begin(ssid.c_str(), password.c_str());
        }

        unsigned long startMs = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if ((millis() - startMs) >= (unsigned long)(timeoutSekunden * 1000)) {
                int wlStatus = (int)WiFi.status();
                Serial.print("WifiLib: Verbindung zu " + ssid + " nach " + String(timeoutSekunden) + "s Timeout fehlgeschlagen. WiFi-Status: " + String(wlStatus));
                switch (wlStatus) {
                    case 1: Serial.print(" (SSID nicht gefunden)");      break;
                    case 4: Serial.print(" (Verbindung fehlgeschlagen)"); break;
                    case 6: Serial.print(" (Getrennt)");                  break;
                }
                Serial.println();
                break;
            }
            delay(500);
        }

        WiFi.removeEvent(disconnectEventId);

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WifiLib: Verbunden mit " + ssid + " | IP: " + WiFi.localIP().toString());
            return true;
        }
        Serial.println("WifiLib: Verbindung fehlgeschlagen, starte AP-Modus...");
    } else {
        Serial.println("WifiLib: Keine gespeicherten Credentials, starte AP-Modus...");
    }

    _startAP(apName);
    return false;
}

void WifiLib::_loadFromNVS() {
    Preferences prefs;
    prefs.begin("wifi_config", true);  // read-only
    ssid     = prefs.getString("ssid", "");
    password = prefs.getString("password", "");
    prefs.end();
    if (ssid.length() > 0) {
        Serial.println("WifiLib: Credentials aus NVS geladen: SSID=" + ssid);
    }
}

void WifiLib::_saveToNVS(const String& newSsid, const String& newPassword) {
    Preferences prefs;
    prefs.begin("wifi_config", false);
    prefs.putString("ssid", newSsid);
    prefs.putString("password", newPassword);
    prefs.end();
    Serial.println("WifiLib: Credentials gespeichert: SSID=" + newSsid);
}

void WifiLib::_startAP(const String& apName) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apName.c_str());

    IPAddress apIp = WiFi.softAPIP();
    Serial.println("WifiLib: AP gestartet: SSID=" + apName + " | IP=" + apIp.toString());
    Serial.println("WifiLib: Verbinde mit AP und oeffne http://" + apIp.toString() + " zur Einrichtung.");

    // Netzwerke scannen fuer die Setup-Seite
    _scannedNetworkCount = WiFi.scanNetworks();
    Serial.println("WifiLib: " + String(_scannedNetworkCount) + " Netzwerke gescannt.");

    // DNS-Server: alle Anfragen zur AP-IP umleiten (captive portal)
    _dnsServer = new DNSServer();
    _dnsServer->start(53, "*", apIp);

    // HTTP-Server fuer die Einrichtungsseite
    _httpServer = new WebServer(80);

    _httpServer->on("/", HTTP_GET, [this]() {
        _letztePortalAktivitaetMs = millis();  // Portal wird benutzt → Retry pausieren
        _httpServer->send(200, "text/html; charset=utf-8", _buildSetupPageHtml());
    });

    _httpServer->on("/save", HTTP_POST, [this]() {
        _letztePortalAktivitaetMs = millis();  // Portal wird benutzt → Retry pausieren
        String newSsid     = _httpServer->arg("ssid");
        String newPassword = _httpServer->arg("password");

        if (newSsid.length() == 0) {
            _httpServer->send(400, "text/html; charset=utf-8",
                "<html><body><h2>Fehler: SSID darf nicht leer sein.</h2>"
                "<a href='/'>Zurueck</a></body></html>");
            return;
        }

        _saveToNVS(newSsid, newPassword);

        _httpServer->send(200, "text/html; charset=utf-8",
            "<html><head><meta charset='utf-8'></head><body>"
            "<h2>Einstellungen gespeichert.</h2>"
            "<p>Das Geraet verbindet sich mit <strong>" + newSsid + "</strong> und startet neu.</p>"
            "</body></html>");

        delay(2000);
        ESP.restart();
    });

    // Captive-Portal-Redirect fuer alle anderen Pfade
    _httpServer->onNotFound([this]() {
        _letztePortalAktivitaetMs = millis();  // Portal wird benutzt → Retry pausieren
        _httpServer->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        _httpServer->send(302, "text/plain", "");
    });

    _httpServer->begin();
    _apModeActive = true;
    // Erste Retry-Frist laeuft ab AP-Start; Portal-Aktivitaet initial setzen, damit ein
    // gerade verbundener Bediener nicht sofort vom ersten Retry gestoert wird.
    _letztesRetryMs = millis();
    _letztePortalAktivitaetMs = millis();
}

String WifiLib::_buildSetupPageHtml() const {
    // Hinweis: R"HTML(...)HTML" als Delimiter, da HTML/JS ")"-Sequenzen enthalten kann
    String html = R"HTML(<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>WiFi-Einrichtung</title>
<style>
  body { font-family: sans-serif; max-width: 420px; margin: 40px auto; padding: 0 16px; background: #f5f5f5; }
  h1 { font-size: 1.4em; color: #333; }
  label { display: block; margin-top: 16px; font-weight: bold; color: #555; }
  select, input[type=text], input[type=password] {
    width: 100%; box-sizing: border-box; padding: 10px; margin-top: 6px;
    border: 1px solid #ccc; border-radius: 6px; font-size: 1em; background: #fff;
  }
  button { margin-top: 24px; width: 100%; padding: 12px; font-size: 1em;
    background: #2196F3; color: #fff; border: none; border-radius: 6px; cursor: pointer; }
  button:hover { background: #1976D2; }
  #manual-ssid { display: none; margin-top: 8px; }
</style>
</head>
<body>
<h1>WiFi-Einrichtung</h1>
<form method="post" action="/save">
  <label for="ssid-select">Netzwerk auswaehlen</label>
  <select id="ssid-select" name="ssid" onchange="toggleManual(this)">
)HTML";

    for (int i = 0; i < _scannedNetworkCount; i++) {
        String netzwerkSsid = WiFi.SSID(i);
        int rssi = WiFi.RSSI(i);
        html += "    <option value=\"" + netzwerkSsid + "\">" + netzwerkSsid +
                " (" + String(rssi) + " dBm)</option>\n";
    }

    html += R"HTML(    <option value="__manual__">Anderes Netzwerk...</option>
  </select>
  <input type="text" id="manual-ssid" name="ssid_manual" placeholder="SSID manuell eingeben">
  <label for="password">Passwort</label>
  <input type="password" id="password" name="password" placeholder="WLAN-Passwort">
  <button type="submit">Verbinden und speichern</button>
</form>
<script>
function toggleManual(sel) {
  var m = document.getElementById('manual-ssid');
  if (sel.value === '__manual__') {
    m.style.display = 'block';
    m.name = 'ssid';
    sel.name = '_ssid_ignored';
  } else {
    m.style.display = 'none';
    m.name = 'ssid_manual';
    sel.name = 'ssid';
  }
}
</script>
</body>
</html>)HTML";

    return html;
}

void WifiLib::handle() {
    if (!_apModeActive) return;
    if (_dnsServer)  _dnsServer->processNextRequest();
    if (_httpServer) _httpServer->handleClient();
    _apReconnectTick();
}

// Nicht-blockierender STA-Reconnect aus dem AP-Modus heraus (siehe Header). State-Machine ueber
// mehrere handle()-Aufrufe: Scan (async) → Verbindungsversuch → Erfolg (AP verlassen) / Timeout
// (im AP bleiben). Greift nur mit gespeicherten NVS-Credentials; ohne Credentials
// (Erstinstallation) und im Env-Var-Modus passiert nichts.
void WifiLib::_apReconnectTick() {
    if (!_apModeActive) return;
    if (!_storedCredMode) return;     // Env-Var-Modus (Mode 1) bleibt unveraendert
    if (ssid.length() == 0) return;   // keine gespeicherten Credentials → unbegrenzter AP-Modus

    const unsigned long RETRY_INTERVALL_MS    = 120000;  // alle 2 min ein Versuch
    const unsigned long PORTAL_AKTIV_MS        = 180000;  // Portal-Nutzung der letzten 3 min pausiert
    const unsigned long CONNECT_TIMEOUT_MS     = 12000;   // max. 12 s pro Versuch

    unsigned long jetzt = millis();

    switch (_apRetryPhase) {
    case _ApRetryPhase::Inaktiv:
        // Wird das Portal gerade benutzt, diesen Zyklus aussetzen (Bediener nicht unterbrechen).
        if (jetzt - _letztePortalAktivitaetMs < PORTAL_AKTIV_MS) return;
        if (jetzt - _letztesRetryMs < RETRY_INTERVALL_MS) return;
        // AP bleibt parallel an (Portal erreichbar), Scan asynchron starten.
        WiFi.mode(WIFI_AP_STA);
        WiFi.scanNetworks(true /*async*/, false /*show_hidden*/);
        _apRetryPhase = _ApRetryPhase::Scannt;
        Serial.println("WifiLib: AP-Reconnect – starte Scan fuer " + ssid + "...");
        break;

    case _ApRetryPhase::Scannt: {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) return;  // -1: laeuft noch
        // Scan fertig (oder fehlgeschlagen): staerksten Knoten der gespeicherten SSID waehlen.
        uint8_t bestBssid[6] = {0};
        bool bestFound = false;
        int bestRssi = -1000;
        for (int i = 0; i < n; i++) {
            if (WiFi.SSID(i) == ssid && WiFi.RSSI(i) > bestRssi) {
                uint8_t* b = WiFi.BSSID(i);
                if (b) { bestRssi = WiFi.RSSI(i); memcpy(bestBssid, b, 6); bestFound = true; }
            }
        }
        WiFi.scanDelete();
        if (bestFound) {
            WiFi.begin(ssid.c_str(), password.c_str(), 0, bestBssid, true);
        } else {
            WiFi.begin(ssid.c_str(), password.c_str());
        }
        _retryConnectStartMs = jetzt;
        _apRetryPhase = _ApRetryPhase::Verbindet;
        break;
    }

    case _ApRetryPhase::Verbindet:
        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("WifiLib: AP-Reconnect erfolgreich – verlasse AP-Modus | IP: "
                           + WiFi.localIP().toString());
            _beendeAPModus();
            _apRetryPhase = _ApRetryPhase::Inaktiv;
            _letztesRetryMs = jetzt;
        } else if (jetzt - _retryConnectStartMs >= CONNECT_TIMEOUT_MS) {
            Serial.println("WifiLib: AP-Reconnect fehlgeschlagen, bleibe im AP-Modus.");
            WiFi.disconnect(false);  // STA-Versuch beenden, AP (WIFI_AP_STA) bleibt erreichbar
            _apRetryPhase = _ApRetryPhase::Inaktiv;
            _letztesRetryMs = jetzt;
        }
        break;
    }
}

// Baut AP/DNS/HTTP ab und wechselt in den reinen STA-Betrieb (nach erfolgreichem Reconnect).
void WifiLib::_beendeAPModus() {
    if (_httpServer) { _httpServer->stop(); delete _httpServer; _httpServer = nullptr; }
    if (_dnsServer)  { _dnsServer->stop();  delete _dnsServer;  _dnsServer = nullptr; }
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    _apModeActive = false;
}

bool WifiLib::isApMode() const {
    return _apModeActive;
}

String WifiLib::getApIP() const {
    return _apModeActive ? WiFi.softAPIP().toString() : "";
}

void WifiLib::reconnect() {
    // Robuster Reconnect: WiFi.reconnect() allein versucht erneut die zuletzt (beim initialen
    // connect) gepinnte BSSID. Ist dieser Knoten weg (Mesh-Roaming, Router-/Hotspot-Neustart mit
    // neuer BSSID), bleibt der Stack dauerhaft in "sta is connecting" haengen und kommt nie zurueck.
    // Daher: alten Verbindungszustand sauber beenden, frisch scannen und auf den jetzt staerksten
    // Knoten der SSID pinnen (gleiche Logik wie connectOrStartAP / _apReconnectTick).
    if (ssid.length() == 0) {
        WiFi.reconnect();  // kein SSID-Kontext (z.B. Env-Var-Modus ohne Auswahl) -> Fallback
        return;
    }

    WiFi.disconnect(false);  // "connecting"-Zustand aufloesen, Radio an lassen

    uint8_t bestBssid[6] = {0};
    bool bestFound = false;
    int bestRssi = -1000;
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i) == ssid && WiFi.RSSI(i) > bestRssi) {
            uint8_t* b = WiFi.BSSID(i);
            if (b) { bestRssi = WiFi.RSSI(i); memcpy(bestBssid, b, 6); bestFound = true; }
        }
    }
    WiFi.scanDelete();

    if (bestFound) {
        Serial.printf("WifiLib: Reconnect – staerkster AP: %02X:%02X:%02X:%02X:%02X:%02X (%d dBm)\n",
            bestBssid[0], bestBssid[1], bestBssid[2], bestBssid[3], bestBssid[4], bestBssid[5], bestRssi);
        WiFi.begin(ssid.c_str(), password.c_str(), 0, bestBssid, true);
    } else {
        Serial.println("WifiLib: Reconnect – kein passender AP gefunden, versuche ohne BSSID-Pinning.");
        WiFi.begin(ssid.c_str(), password.c_str());
    }
}

void WifiLib::deleteCredentials() {
    Preferences prefs;
    prefs.begin("wifi_config", false);
    prefs.clear();
    prefs.end();
    Serial.println("WifiLib: Gespeicherte WiFi-Credentials geloescht. Naechster Start: AP-Modus.");
}

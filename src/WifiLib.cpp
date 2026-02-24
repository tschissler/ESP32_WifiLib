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
        Serial.println("No known WiFi networks defined, will not connect to Wifi.");
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
         while (WiFi.status() != WL_CONNECTED) {
            WiFi.begin(ssid.c_str(), password.c_str(), 0, bssid, true);
            delay(1000);
            if (WiFi.status() == WL_CONNECTED) {
                break;
            }
            Serial.println("Could not connect to Wifi " + ssid + " - password might be incorrect, retrying...");
        }
    } else {
        while (WiFi.status() != WL_CONNECTED) {
            WiFi.begin(ssid.c_str(), password.c_str());
            delay(1000);
            if (WiFi.status() == WL_CONNECTED) {
                break;
            }
            Serial.println("Could not connect to Wifi " + ssid + " - password might be incorrect, retrying...");
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
        // Kein BSSID-Pinning: Mesh-kompatibel, der Treiber waehlt den besten AP
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), password.c_str());

        unsigned long startMs = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if ((millis() - startMs) >= (unsigned long)(timeoutSekunden * 1000)) {
                Serial.println("WifiLib: Verbindung zu " + ssid + " nach " + String(timeoutSekunden) + "s Timeout fehlgeschlagen.");
                break;
            }
            delay(500);
        }

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
        _httpServer->send(200, "text/html; charset=utf-8", _buildSetupPageHtml());
    });

    _httpServer->on("/save", HTTP_POST, [this]() {
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
        _httpServer->sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/");
        _httpServer->send(302, "text/plain", "");
    });

    _httpServer->begin();
    _apModeActive = true;
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
}

bool WifiLib::isApMode() const {
    return _apModeActive;
}

String WifiLib::getApIP() const {
    return _apModeActive ? WiFi.softAPIP().toString() : "";
}

void WifiLib::reconnect() {
    WiFi.reconnect();
}

void WifiLib::deleteCredentials() {
    Preferences prefs;
    prefs.begin("wifi_config", false);
    prefs.clear();
    prefs.end();
    Serial.println("WifiLib: Gespeicherte WiFi-Credentials geloescht. Naechster Start: AP-Modus.");
}

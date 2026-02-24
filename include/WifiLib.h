#pragma once
#include <Arduino.h>
#include <map>

// Vorwaertsdeklarationen um Includes in Header zu vermeiden
class WebServer;
class DNSServer;

class WifiLib {
public:
    // Modus 1: Bekannte Netzwerke aus Umgebungsvariable (bestehende Projekte)
    WifiLib(const String& wifiPasswords);
    void scanAndSelectNetwork();
    void connect();
    String getSSID() const;
    String getPassword() const;
    String getLocalIP() const;
    String getBSSID() const;

    // Modus 2: Gespeicherte Credentials (NVS) oder AP-Einrichtungsmodus
    // Wird fuer Geraete verwendet, die in verschiedenen Umgebungen eingesetzt werden.
    WifiLib();

    // Versucht Verbindung mit gespeicherten NVS-Credentials herzustellen.
    // Falls keine Credentials gespeichert oder Verbindung schlaegt fehl:
    // Oeffnet einen Konfigurations-AP (captive portal) zur Ersteinrichtung.
    // Mesh-kompatibel: verbindet per SSID, kein BSSID-Pinning.
    // Gibt true zurueck bei erfolgreicher Verbindung, false wenn AP-Modus gestartet wurde.
    bool connectOrStartAP(const String& apName = "ESP32-Setup", int timeoutSekunden = 30);

    // Muss regelmaessig aus loop() aufgerufen werden (kein Blocking).
    // Verarbeitet im AP-Modus DNS- und HTTP-Anfragen.
    // Im Normalbetrieb: no-op.
    void handle();

    // true wenn der AP-Einrichtungsmodus aktiv ist.
    bool isApMode() const;

    // Nicht-blockierender Reconnect-Versuch (ruft WiFi.reconnect() auf).
    // Fuer den Einsatz im Control Loop statt der blockierenden while-Schleife.
    void reconnect();

    // Loescht die im NVS gespeicherten WiFi-Credentials.
    // Beim naechsten Neustart wird der AP-Einrichtungsmodus geoeffnet.
    void deleteCredentials();

private:
    // Modus 1: Env-Var-basiert
    String ssid;
    String password;
    String passwords;
    uint8_t bssid[6];
    bool bssidSet;
    void parseWifis(std::map<String, String> &knownWifis);

    // Modus 2: NVS / AP-Modus
    bool _storedCredMode;
    bool _apModeActive;
    WebServer* _httpServer;
    DNSServer* _dnsServer;
    int _scannedNetworkCount;

    void _loadFromNVS();
    void _saveToNVS(const String& newSsid, const String& newPassword);
    void _startAP(const String& apName);
    String _buildSetupPageHtml() const;
};

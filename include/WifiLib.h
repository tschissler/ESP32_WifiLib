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
    // Mesh-robust: scannt die gespeicherte SSID und pinnt den staerksten BSSID.
    // Gibt true zurueck bei erfolgreicher Verbindung, false wenn AP-Modus gestartet wurde.
    bool connectOrStartAP(const String& apName = "ESP32-Setup", int timeoutSekunden = 30);

    // Wie connectOrStartAP, aber OHNE AP-Fallback: nur ein (mesh-robuster) STA-Verbindungsversuch
    // mit den gespeicherten NVS-Credentials. Gibt true bei Verbindung, false sonst (kein AP-Start).
    // Fuer Geraete, die bei WLAN-Ausfall NICHT automatisch in den AP-Modus fallen duerfen (z.B. weil
    // eine andere Datenquelle weiterlaufen muss); der Aufrufer steuert den AP dann per startAP()/stopAP().
    bool connectOnly(int timeoutSekunden = 30);

    // Startet den Konfigurations-AP (captive portal) auf Anforderung – z.B. ueber einen Taster am Geraet,
    // wenn der automatische AP-Fallback bewusst unterdrueckt wurde (connectOnly). No-op, wenn AP bereits laeuft.
    // autoStopBeiInaktivitaetMs > 0: der AP beendet sich in handle() selbst, wenn so lange keine Portal-
    // Aktivitaet (Seitenaufruf/Speichern) stattfand – Schutz davor, dass ein vergessener AP das Geraet
    // dauerhaft im AP-Modus haelt. 0 (Default) = kein Auto-Stop, AP bleibt bis stopAP()/Reconnect/Reboot.
    void startAP(const String& apName = "ESP32-Setup", unsigned long autoStopBeiInaktivitaetMs = 0);

    // Beendet den AP-Modus und wechselt zurueck in den reinen STA-Betrieb (Gegenstueck zu startAP()).
    // No-op, wenn kein AP aktiv ist.
    void stopAP();

    // Muss regelmaessig aus loop() aufgerufen werden (kein Blocking).
    // Verarbeitet im AP-Modus DNS- und HTTP-Anfragen.
    // Im Normalbetrieb: no-op.
    void handle();

    // true wenn der AP-Einrichtungsmodus aktiv ist.
    bool isApMode() const;

    // IP-Adresse des Konfigurations-AP (nur gueltig wenn isApMode() == true).
    // Typischerweise 192.168.4.1 (ESP32-Default), kann aber per softAPConfig() abweichen.
    String getApIP() const;

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

    // Diagnose: loggt die komplette Scan-Liste (alle sichtbaren APs inkl. ALLER Mesh-Knoten/BSSIDs
    // derselben SSID) mit RSSI/Kanal. Knoten der Ziel-SSID werden mit ">>" markiert, damit die
    // BSSID-Auswahl am Monitor nachvollziehbar ist. Erwartet ein bereits abgeschlossenes
    // WiFi.scanNetworks() (n = Anzahl Ergebnisse) und loescht den Scan NICHT.
    void _logScanResults(int n, const String& zielSsid) const;

    // AP-Modus-Reconnect (nicht-blockierend): Faellt ein Geraet mit gespeicherten NVS-Credentials
    // in den AP-Modus (z. B. Router beim Boot kurz weg), versucht es periodisch eine STA-Verbindung,
    // statt dauerhaft im AP-Modus zu haengen. Greift NUR bei "Stored-Cred-Modus + Credentials
    // vorhanden"; Erstinstallation (keine Credentials) und der Env-Var-Modus bleiben unveraendert.
    // Wird aus handle() getickt; der AP laeuft waehrend der Versuche als WIFI_AP_STA weiter (Portal
    // bleibt erreichbar). Ein gerade genutztes Portal pausiert den Retry.
    enum class _ApRetryPhase { Inaktiv, Scannt, Verbindet };
    _ApRetryPhase _apRetryPhase = _ApRetryPhase::Inaktiv;
    unsigned long _letztesRetryMs = 0;
    unsigned long _letztePortalAktivitaetMs = 0;
    unsigned long _retryConnectStartMs = 0;
    unsigned long _apAutoStopInaktivMs = 0;  // >0: AP nach so langer Portal-Inaktivitaet selbst beenden (startAP)
    void _apReconnectTick();
    void _beendeAPModus();
};

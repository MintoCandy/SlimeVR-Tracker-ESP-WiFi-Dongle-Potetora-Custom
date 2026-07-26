/*
	SlimeVR ESP tracker WiFi dongle firmware.
*/

#include "HID.h"
#include "WifiDongleConfig.h"
#include "button.h"
#include "configuration.h"
#include "error_codes.h"
#include "led.h"
#include "packetHandling.h"
#include "logging/Logger.h"

#include <Arduino.h>
#include <USB.h>
#include <USBCDC.h>

#include <Preferences.h> //WebConfig用
#include "WebUI.h"

#ifdef USE_OFFICIAL_PROXY
  #include "SlimeServerEmu.h"
#else
  #include "WifiCommunication.h"
#endif

String currentSsid;
String currentPassword;

void loadWiFiSettings() {
    Preferences prefs;
    prefs.begin("wifi_config", true); // 読み込み専用モードで開く
    
    // キーが存在するか確認してから読み込むことで、システムエラーログを回避
    if (prefs.isKey("ssid")) {
        currentSsid = prefs.getString("ssid");
    } else {
        currentSsid = WifiDongleConfig::apSsid;
    }

    if (prefs.isKey("password")) {
        currentPassword = prefs.getString("password");
    } else {
        currentPassword = WifiDongleConfig::apPassword;
    }
    
    prefs.end();
    
    Serial.printf("[WiFi] Loaded SSID: %s\n", currentSsid.c_str());
}

HIDDevice hidDevice;
Button &button = Button::getInstance();
LED led;
SlimeVR::Logging::Logger logger("Main");

#ifdef USE_OFFICIAL_PROXY
  SlimeServerEmu &comm = SlimeServerEmu::getInstance();
#else
  WifiCommunication &comm = WifiCommunication::getInstance();
#endif

[[noreturn]] void fail(ErrorCodes errorCode) {
    led.displayError(errorCode);
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting up " USB_PRODUCT "...");
    Configuration::getInstance().setup();
    hidDevice.begin();
    USB.begin();

    button.begin();

#ifndef USE_OFFICIAL_PROXY
    button.onLongPress([]() {
        if (!comm.isInPairingMode()) {
            Serial.println("Pairing mode enabled");
            comm.enterPairingMode();
            led.sendContinuousBlinks(0.1f, 0.5f);
        } else {
            Serial.println("Pairing mode disabled");
            comm.exitPairingMode();
            led.stopBlinking();
        }
    });
#endif

    button.onMultiPress([](size_t pressCount) {
        if (pressCount == 5) {
            Serial.println("Trackers reset");
            Configuration::getInstance().resetTrackers(); // 既存のリセット
            Preferences prefs; 
            prefs.begin("slime_pairs", false);//"slime_pairs" のペアリング情報も消去
            prefs.clear();
            prefs.end();
            Serial.println("Custom tracker pairings cleared");

            led.sendBlinks(5, 0.2f, 0.1f);
            return;
        }
        if (pressCount == 7) {
            Serial.println("[Reset] Executing Factory Reset by 7 clicks!");
            Preferences prefs;
            prefs.begin("wifi_config", false);
            prefs.clear(); 
            prefs.end();
            
            // LEDを点滅させる
            led.sendBlinks(10, 0.1f, 0.1f);
            delay(1000); 
            ESP.restart(); // リセット完了後、自動的に再起動
            return;
        }
    });

    led.begin();
    led.setState(false);

    //Wi-Fiを起動する直前で、設定を読み込む
    loadWiFiSettings();

    //WifiDongleConfig の固定値ではなく、読み込んだ変数を使うように変更
    ErrorCodes result = comm.begin(
        currentSsid.c_str(),           // 変更箇所
        currentPassword.c_str(),       // 変更箇所
        WifiDongleConfig::apChannel,
        WifiDongleConfig::maxTrackers,
        WifiDongleConfig::apHidden
    );
    if (result != ErrorCodes::NO_ERROR) {
        fail(result);
    }

    //Wi-Fiが正常に立ち上がったら、裏側でWebUIを起動する
    setupWebServer();

#ifndef USE_OFFICIAL_PROXY
    comm.onTrackerPaired([&]() {
        Serial.println("New tracker paired");
        led.sendBlinks(3, 0.1f);
    });
#endif

    comm.onTrackerConnected(
        [&](uint8_t trackerId, const uint8_t *trackerMacAddress) {
            bool isNew = PacketHandling::getInstance().registerTracker(trackerId, trackerMacAddress);
            if (isNew) {
                Serial.println("New tracker connected");
                led.sendBlinks(2, 0.1f);
            }
        });

#ifndef USE_OFFICIAL_PROXY
    comm.onInfoReceived(
        [&](const uint8_t *info) {
            PacketHandling::getInstance().insertInfo(info);
        });
    comm.onPacketReceived(
        [&](const uint8_t *packet) {
            PacketHandling::getInstance().insert(packet);
        });
#endif
    Serial.println("Boot complete");
}

void loop() {
    button.update();
    led.update();
    comm.update();
    PacketHandling::getInstance().tick(hidDevice);
}

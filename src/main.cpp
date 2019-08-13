#include <Arduino.h>

#if defined(ESP32)
#include <SPIFFS.h>
#else
#include <FS.h>
#endif

#include "WifiServer.h"
#include "LocalDNS.h"
#include "MyMatrix.h"
#include "EffectsManager.h"
#include "Settings.h"
#include "GyverUdp.h"
#include "GyverTimer.h"

#include "GyverButton.h"
#include "LampWebServer.h"

namespace  {

uint8_t eepromInitialization = 0xaa; // change this value to reset EEPROM settings to defaults

const char* wifiSetupName = "Fire Lamp";
const char* wifiOndemandName = "Fire Lamp AP";
const char* wifiOndemandPassword = "ondemand";

uint16_t webServerPort = 80;
uint16_t webServerFallbackPort = 8080;
uint16_t webSocketPort = 8000;
uint16_t udpServerPort = 8888;

const char* localHostname = "firelamp";

uint8_t matrixWidth = 16;
uint8_t matrixHeight = 16;

uint8_t maxBrightness = 80;
uint32_t maxCurrent = 1000;

#if defined(ESP32)
const uint8_t btnPin = 15;
#else
const uint8_t btnPin = 1;
#endif

GButton *button = nullptr;

const char* poolServerName = "europe.pool.ntp.org";
int timeOffset = 3 * 3600; // GMT + 3
int updateInterval = 60 * 1000; // 1 min
uint32_t timerInterval = 5 * 60 * 1000; // 5 min

int stepDirection = 1;
bool isHolding = false;

void processButton()
{
    button->tick();
    if (button->isSingle()) {
        Serial.println("Single button");
        Settings::masterSwitch = !Settings::masterSwitch;
        if (!Settings::masterSwitch) {
            myMatrix->clear();
            myMatrix->show();
        }
    }
    if (!Settings::masterSwitch) {
        return;
    }
    if (button->isDouble()) {
        Serial.println("Double button");
        EffectsManager::Next();
        Settings::SaveLater();
    }
    if (button->isTriple()) {
        Serial.println("Triple button");
        EffectsManager::Previous();
        Settings::SaveLater();
    }
    return;
    if (button->isHolded()) {
        Serial.println("Holded button");
        isHolding = true;
        const uint8_t brightness = Settings::CurrentEffectSettings()->effectBrightness;
        if (brightness <= 1) {
            stepDirection = 1;
        } else if (brightness == 255) {
            stepDirection = -1;
        }
    }
    if (isHolding && button->isStep()) {
        uint8_t brightness = Settings::CurrentEffectSettings()->effectBrightness;
        if (stepDirection < 0 && brightness == 1) {
            return;
        }
        if (stepDirection > 0 && brightness == 255) {
            return;
        }
        brightness += stepDirection;
        Serial.printf("Step button %d. brightness: %u\n", stepDirection, brightness);
        Settings::CurrentEffectSettings()->effectBrightness = brightness;
        myMatrix->setBrightness(brightness);
    }
    if (button->isRelease() && isHolding) {
        Serial.println("Release button");
        Settings::SaveLater();
        isHolding = false;
    }
}

}

void setup() {

    if(!SPIFFS.begin()) {
        Serial.println("An Error has occurred while mounting SPIFFS");
        return;
    }

#if defined(ESP8266)
    ESP.wdtDisable();
    ESP.wdtEnable(0);
#endif

    WifiServer::Initialize(wifiSetupName);
    Serial.printf("WiFi connected: %s\n", WifiServer::isConnected() ? "true" : "false");

    if (!LocalDNS::Begin(localHostname)) {
        Serial.println("An Error has occurred while initializing mDNS");
        return;
    }

    if (!WifiServer::isConnected()) {
        LocalDNS::AddService("setup", "tcp", webServerPort);
        LocalDNS::AddService("http", "tcp", webServerFallbackPort);
        LampWebServer::Initialize(webServerFallbackPort, webSocketPort);
        return;
    }

    Serial.flush();

    GyverTimer::Initialize(poolServerName, timeOffset, updateInterval, timerInterval);

    LampWebServer::Initialize(webServerPort, webSocketPort);
    GyverUdp::Initiazlize(udpServerPort);
    LocalDNS::AddService("http", "tcp", webServerPort);
    LocalDNS::AddService("ws", "tcp", webSocketPort);
    LocalDNS::AddService("app", "udp", udpServerPort);

    randomSeed(micros());

    MyMatrix::Initialize(
        matrixWidth, matrixHeight,
//                NEO_MATRIX_TOP + NEO_MATRIX_RIGHT +
//                NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG);
//                NEO_MATRIX_TOP + NEO_MATRIX_LEFT +
//                NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG);
                NEO_MATRIX_BOTTOM + NEO_MATRIX_RIGHT +
                NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG);
//                NEO_MATRIX_BOTTOM + NEO_MATRIX_LEFT +
//                NEO_MATRIX_COLUMNS + NEO_MATRIX_ZIGZAG);
//                NEO_MATRIX_TOP + NEO_MATRIX_RIGHT +
//                NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG);
//                NEO_MATRIX_TOP + NEO_MATRIX_LEFT +
//                NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG);
//                NEO_MATRIX_BOTTOM + NEO_MATRIX_RIGHT +
//                NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG);
//                NEO_MATRIX_BOTTOM + NEO_MATRIX_LEFT +
//                NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG);
    myMatrix->setBrightness(maxBrightness);
    myMatrix->setCurrentLimit(maxCurrent);
    myMatrix->setRotation(3);

    myMatrix->matrixTest();

    EffectsManager::Initialize();

    Serial.begin(115200);
    Serial.println("Happy debugging!");
    Serial.flush();

    Settings::Initialize(eepromInitialization);
    EffectsManager::ActivateEffect(Settings::currentEffect);

    button = new GButton(btnPin, GButton::PullTypeLow, GButton::DefaultStateOpen);
    button->setTickMode(false);
    button->setStepTimeout(20);
}

void loop() {
#if defined(ESP8266)
    ESP.wdtFeed();
#endif

    WifiServer::Process();
    lampWebServer->Process();


    if (!lampWebServer->isUpdating()) {
        GyverUdp::Process();
        GyverTimer::Process();
        processButton();

        if (Settings::masterSwitch) {
            EffectsManager::Process();
        }

        Settings::Process();
    }
}

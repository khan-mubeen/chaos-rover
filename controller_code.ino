#include <WiFi.h>
#include <esp_now.h>

// Joystick pins
const int JOY_X_PIN = 34;
const int JOY_Y_PIN = 35;

// Button pins
const int SPEED_UP_BTN = 25;
const int SPEED_DOWN_BTN = 26;

// Toggle switch pin
const int MODE_SWITCH = 27;

// Speed LED bar graph pins
// LED 1  -> D13
// LED 2  -> D14
// LED 3  -> D18
// LED 4  -> D4
// LED 5  -> D19
// LED 6  -> D21
// LED 7  -> D22
// LED 8  -> D23
// LED 9  -> D32
// LED 10 -> D33
const int SPEED_LED_PINS[10] = {
  13, 14, 18, 4, 19,
  21, 22, 23, 32, 33
};

// Chaos timer LED bar graph groups
// Each pin controls 2 LED segments
// D5  -> LEDs 1 + 2
// D15 -> LEDs 3 + 4
// D2  -> LEDs 5 + 6
// D12 -> LEDs 7 + 8
// RX2 -> LEDs 9 + 10
const int TIMER_LED_GROUPS[5] = {
  5,   // D5
  15,  // D15
  2,   // D2
  12,  // D12
  16   // RX2 / GPIO16
};

const unsigned long CHAOS_INTERVAL = 20000; // 20 seconds

int speedLevel = 5;  // 1 to 10

bool lastUpState = HIGH;
bool lastDownState = HIGH;

unsigned long lastButtonTime = 0;
const unsigned long debounceDelay = 200;

unsigned long lastSendTime = 0;
const unsigned long sendInterval = 50;

unsigned long chaosStartTime = 0;
int previousMode = -1;

// Rover ESP32 MAC Address
uint8_t roverAddress[] = {0xF4, 0x2D, 0xC9, 0x76, 0x63, 0xD8};

typedef struct ControlData {
  int xValue;
  int yValue;
  int speedLevel;
  int mode;  // 0 = Normal, 1 = Chaos
} ControlData;

ControlData dataToSend;

void setup() {
  Serial.begin(115200);

  pinMode(SPEED_UP_BTN, INPUT_PULLUP);
  pinMode(SPEED_DOWN_BTN, INPUT_PULLUP);
  pinMode(MODE_SWITCH, INPUT_PULLUP);

  for (int i = 0; i < 10; i++) {
    pinMode(SPEED_LED_PINS[i], OUTPUT);
    digitalWrite(SPEED_LED_PINS[i], LOW);
  }

  for (int i = 0; i < 5; i++) {
    pinMode(TIMER_LED_GROUPS[i], OUTPUT);
    digitalWrite(TIMER_LED_GROUPS[i], LOW);
  }

  updateSpeedBarGraph();
  clearTimerBar();

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, roverAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("Failed to add rover peer");
    return;
  }

  Serial.println("Controller ESP32 ready");
  printSpeed();
}

void loop() {
  handleButtons();

  int modeValue = getModeValue();

  updateSpeedBarGraph();
  updateChaosTimerBar(modeValue);

  if (millis() - lastSendTime >= sendInterval) {
    sendControlData(modeValue);
    lastSendTime = millis();
  }
}

int getModeValue() {
  int switchState = digitalRead(MODE_SWITCH);

  if (switchState == HIGH) {
    return 0;  // Normal Mode
  } else {
    return 1;  // Chaos Mode
  }
}

void handleButtons() {
  bool upState = digitalRead(SPEED_UP_BTN);
  bool downState = digitalRead(SPEED_DOWN_BTN);

  unsigned long now = millis();

  if (now - lastButtonTime > debounceDelay) {
    if (lastUpState == HIGH && upState == LOW) {
      if (speedLevel < 10) {
        speedLevel++;
      }
      printSpeed();
      lastButtonTime = now;
    }

    if (lastDownState == HIGH && downState == LOW) {
      if (speedLevel > 1) {
        speedLevel--;
      }
      printSpeed();
      lastButtonTime = now;
    }
  }

  lastUpState = upState;
  lastDownState = downState;
}

void sendControlData(int modeValue) {
  int x = analogRead(JOY_X_PIN);
  int y = analogRead(JOY_Y_PIN);

  dataToSend.xValue = x;
  dataToSend.yValue = y;
  dataToSend.speedLevel = speedLevel;
  dataToSend.mode = modeValue;

  esp_err_t result = esp_now_send(roverAddress, (uint8_t*)&dataToSend, sizeof(dataToSend));

  Serial.print("Sent X = ");
  Serial.print(x);
  Serial.print(" | Y = ");
  Serial.print(y);
  Serial.print(" | Speed = ");
  Serial.print(speedLevel);
  Serial.print(" | Mode = ");

  if (modeValue == 0) {
    Serial.print("NORMAL");
  } else {
    Serial.print("CHAOS");
  }

  Serial.print(" | Status = ");

  if (result == ESP_OK) {
    Serial.println("OK");
  } else {
    Serial.println("FAILED");
  }
}

void updateSpeedBarGraph() {
  for (int i = 0; i < 10; i++) {
    if (i < speedLevel) {
      digitalWrite(SPEED_LED_PINS[i], HIGH);
    } else {
      digitalWrite(SPEED_LED_PINS[i], LOW);
    }
  }
}

void updateChaosTimerBar(int modeValue) {
  if (modeValue != previousMode) {
    previousMode = modeValue;

    if (modeValue == 1) {
      chaosStartTime = millis();
      showTimerLevel(5);
      Serial.println("Controller timer: CHAOS START");
    } else {
      clearTimerBar();
      Serial.println("Controller timer: NORMAL MODE");
    }
  }

  if (modeValue == 0) {
    clearTimerBar();
    return;
  }

  unsigned long elapsed = (millis() - chaosStartTime) % CHAOS_INTERVAL;

  int level;

  if (elapsed < 4000) {
    level = 5;  // 10 LEDs
  }
  else if (elapsed < 8000) {
    level = 4;  // 8 LEDs
  }
  else if (elapsed < 12000) {
    level = 3;  // 6 LEDs
  }
  else if (elapsed < 16000) {
    level = 2;  // 4 LEDs
  }
  else {
    level = 1;  // 2 LEDs
  }

  showTimerLevel(level);
}

void showTimerLevel(int level) {
  level = constrain(level, 0, 5);

  for (int i = 0; i < 5; i++) {
    if (i < level) {
      digitalWrite(TIMER_LED_GROUPS[i], HIGH);
    } else {
      digitalWrite(TIMER_LED_GROUPS[i], LOW);
    }
  }
}

void clearTimerBar() {
  showTimerLevel(0);
}

void printSpeed() {
  Serial.print("Speed Level = ");
  Serial.println(speedLevel);
}
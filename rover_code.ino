#include <WiFi.h>
#include <esp_now.h>
#include <ESP32Servo.h>
#include <esp_system.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <LiquidCrystal_I2C.h>
#include <Adafruit_NeoPixel.h>

// Servo pins
const int LEFT_SERVO_PIN = 18;
const int RIGHT_SERVO_PIN = 19;

const int LEFT_STOP = 90;
const int RIGHT_STOP = 94;

// Active buzzer pin
const int BUZZER_PIN = 25;

// LED strip
const int LED_STRIP_PIN = 4;
const int NUM_LEDS = 32;

Adafruit_NeoPixel strip(NUM_LEDS, LED_STRIP_PIN, NEO_GRB + NEO_KHZ800);

// I2C pins
const int I2C_SDA = 21;
const int I2C_SCL = 22;

// OLED settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool oledReady = false;

// LCD settings
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Chaos settings
const unsigned long CHAOS_INTERVAL = 20000; // 20 seconds

Servo leftServo;
Servo rightServo;

typedef struct ControlData {
  int xValue;
  int yValue;
  int speedLevel;
  int mode;  // 0 = Normal, 1 = Chaos
} ControlData;

ControlData receivedData;

unsigned long lastReceiveTime = 0;
unsigned long lastChaosShuffleTime = 0;

int previousMode = -1;
int ledModeShown = -2;

// Direction names
const int DIR_STOP = 0;
const int DIR_FORWARD = 1;
const int DIR_BACKWARD = 2;
const int DIR_LEFT = 3;
const int DIR_RIGHT = 4;

// Chaos mapping
int chaosMap[5];

// LCD scrolling text
String normalTitle = "NORMAL MODE";
String chaosTitle = "CHAOS MODE";

String normalText = "Everything is stable. The rover is listening to you.   ";
String chaosText  = "Good luck, driver. The rover has its own ideas now.   ";

String currentTitle = "NORMAL MODE";
String currentMessage = "Everything is stable. The rover is listening to you.   ";

unsigned long lastLCDScrollTime = 0;
const unsigned long lcdScrollInterval = 250;
int scrollIndex = 0;
int lcdModeShown = -1;

void setup() {
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  leftServo.attach(LEFT_SERVO_PIN);
  rightServo.attach(RIGHT_SERVO_PIN);
  stopServos();

  // Start LED strip
  strip.begin();
  strip.setBrightness(80);   
  strip.show();
  setLEDMode(-1);            // off at startup

  // Start I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Start LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Chaos Rover");
  lcd.setCursor(0, 1);
  lcd.print("System Ready");

  // Start OLED
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    oledReady = true;
    drawNormalFace();
    Serial.println("OLED ready");
  } else {
    oledReady = false;
    Serial.println("OLED not found, continuing without OLED");
  }

  delay(1500);
  setLCDMode(0);

  randomSeed(esp_random());

  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  createNormalChaosMap();

  Serial.println("Rover ESP32 ready with OLED, LCD, buzzer, LED strip, and 20s chaos mode");
}

void loop() {
  if (millis() - lastReceiveTime > 500) {
    stopServos();
    digitalWrite(BUZZER_PIN, LOW);
    setLEDMode(-1); // LED strip off if controller signal lost
  } else {
    handleModeAndControl();
  }

  updateLCDScroll();

  delay(20);
}

void OnDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len != sizeof(ControlData)) {
    Serial.print("Ignored bad packet. Length = ");
    Serial.println(len);
    return;
  }

  memcpy(&receivedData, incomingData, sizeof(receivedData));

  if (receivedData.xValue < 0 || receivedData.xValue > 4095) {
    Serial.println("Ignored bad X value");
    return;
  }

  if (receivedData.yValue < 0 || receivedData.yValue > 4095) {
    Serial.println("Ignored bad Y value");
    return;
  }

  if (receivedData.speedLevel < 1 || receivedData.speedLevel > 10) {
    Serial.println("Ignored bad speed value");
    return;
  }

  if (receivedData.mode < 0 || receivedData.mode > 1) {
    Serial.println("Ignored bad mode value");
    return;
  }

  lastReceiveTime = millis();
}

void handleModeAndControl() {
  int currentMode = receivedData.mode;

  if (currentMode != previousMode) {
    previousMode = currentMode;

    if (currentMode == 0) {
      Serial.println("MODE CHANGED: NORMAL MODE");

      digitalWrite(BUZZER_PIN, LOW);
      setLEDMode(0); // white

      createNormalChaosMap();
      drawNormalFace();
      setLCDMode(0);
    } else {
      Serial.println("MODE CHANGED: CHAOS MODE");

      digitalWrite(BUZZER_PIN, HIGH);
      setLEDMode(1); // red

      shuffleChaosMap();
      drawChaosFace();
      setLCDMode(1);
      lastChaosShuffleTime = millis();
    }
  }

  if (currentMode == 1) {
    digitalWrite(BUZZER_PIN, HIGH);
    setLEDMode(1);

    if (millis() - lastChaosShuffleTime >= CHAOS_INTERVAL) {
      Serial.println("CHAOS TIMER FINISHED: Mapping reshuffled");
      shuffleChaosMap();
      drawChaosFace();
      lastChaosShuffleTime = millis();
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    setLEDMode(0);
  }

  int inputDirection = getJoystickDirection(receivedData.xValue, receivedData.yValue);
  int outputDirection;

  if (currentMode == 0) {
    outputDirection = inputDirection;
  } else {
    outputDirection = chaosMap[inputDirection];
  }

  driveDirection(outputDirection, receivedData.speedLevel);
}

int getJoystickDirection(int x, int y) {
  if (x > 3000) {
    return DIR_FORWARD;
  }
  else if (x < 1000) {
    return DIR_BACKWARD;
  }
  else if (y > 3000) {
    return DIR_LEFT;
  }
  else if (y < 1000) {
    return DIR_RIGHT;
  }
  else {
    return DIR_STOP;
  }
}

void driveDirection(int direction, int speedLevel) {
  int offset = map(speedLevel, 1, 10, 20, 90);

  if (direction == DIR_FORWARD) {
    leftServo.write(constrain(LEFT_STOP + offset, 0, 180));
    rightServo.write(constrain(RIGHT_STOP - offset, 0, 180));
  }
  else if (direction == DIR_BACKWARD) {
    leftServo.write(constrain(LEFT_STOP - offset, 0, 180));
    rightServo.write(constrain(RIGHT_STOP + offset, 0, 180));
  }
  else if (direction == DIR_LEFT) {
    leftServo.write(constrain(LEFT_STOP - offset, 0, 180));
    rightServo.write(constrain(RIGHT_STOP - offset, 0, 180));
  }
  else if (direction == DIR_RIGHT) {
    leftServo.write(constrain(LEFT_STOP + offset, 0, 180));
    rightServo.write(constrain(RIGHT_STOP + offset, 0, 180));
  }
  else {
    stopServos();
  }
}

void stopServos() {
  leftServo.write(LEFT_STOP);
  rightServo.write(RIGHT_STOP);
}

void createNormalChaosMap() {
  chaosMap[DIR_STOP] = DIR_STOP;
  chaosMap[DIR_FORWARD] = DIR_FORWARD;
  chaosMap[DIR_BACKWARD] = DIR_BACKWARD;
  chaosMap[DIR_LEFT] = DIR_LEFT;
  chaosMap[DIR_RIGHT] = DIR_RIGHT;
}

void shuffleChaosMap() {
  int directions[4] = {
    DIR_FORWARD,
    DIR_BACKWARD,
    DIR_LEFT,
    DIR_RIGHT
  };

  for (int i = 3; i > 0; i--) {
    int j = random(0, i + 1);

    int temp = directions[i];
    directions[i] = directions[j];
    directions[j] = temp;
  }

  chaosMap[DIR_STOP] = DIR_STOP;
  chaosMap[DIR_FORWARD] = directions[0];
  chaosMap[DIR_BACKWARD] = directions[1];
  chaosMap[DIR_LEFT] = directions[2];
  chaosMap[DIR_RIGHT] = directions[3];

  printChaosMap();
}

void printChaosMap() {
  Serial.println("Current Chaos Mapping:");

  Serial.print("Joystick Forward  -> ");
  printDirectionName(chaosMap[DIR_FORWARD]);

  Serial.print("Joystick Backward -> ");
  printDirectionName(chaosMap[DIR_BACKWARD]);

  Serial.print("Joystick Left     -> ");
  printDirectionName(chaosMap[DIR_LEFT]);

  Serial.print("Joystick Right    -> ");
  printDirectionName(chaosMap[DIR_RIGHT]);

  Serial.println("----------------------");
}

void printDirectionName(int direction) {
  if (direction == DIR_FORWARD) {
    Serial.println("FORWARD");
  }
  else if (direction == DIR_BACKWARD) {
    Serial.println("BACKWARD");
  }
  else if (direction == DIR_LEFT) {
    Serial.println("LEFT");
  }
  else if (direction == DIR_RIGHT) {
    Serial.println("RIGHT");
  }
  else {
    Serial.println("STOP");
  }
}

void setLCDMode(int mode) {
  if (mode == lcdModeShown) {
    return;
  }

  lcdModeShown = mode;
  scrollIndex = 0;

  if (mode == 0) {
    currentTitle = normalTitle;
    currentMessage = normalText;
  } else {
    currentTitle = chaosTitle;
    currentMessage = chaosText;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(currentTitle);
}

void updateLCDScroll() {
  if (millis() - lastLCDScrollTime < lcdScrollInterval) {
    return;
  }

  lastLCDScrollTime = millis();

  String padded = "                " + currentMessage + "                ";

  if (scrollIndex > padded.length() - 16) {
    scrollIndex = 0;
  }

  lcd.setCursor(0, 1);
  lcd.print(padded.substring(scrollIndex, scrollIndex + 16));

  scrollIndex++;
}

void setLEDMode(int mode) {
  if (mode == ledModeShown) {
    return;
  }

  ledModeShown = mode;

  if (mode == 0) {
    // Normal mode: dim white
    setStripColor(80, 80, 80);
  }
  else if (mode == 1) {
    // Chaos mode: red
    setStripColor(180, 0, 0);
  }
  else {
    // Off
    setStripColor(0, 0, 0);
  }
}

void setStripColor(int r, int g, int b) {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, strip.Color(r, g, b));
  }

  strip.show();
}

void drawNormalFace() {
  if (!oledReady) return;

  display.clearDisplay();

  display.drawCircle(64, 32, 26, SSD1306_WHITE);

  display.fillCircle(54, 25, 4, SSD1306_WHITE);
  display.fillCircle(74, 25, 4, SSD1306_WHITE);

  display.drawLine(50, 40, 56, 46, SSD1306_WHITE);
  display.drawLine(56, 46, 72, 46, SSD1306_WHITE);
  display.drawLine(72, 46, 78, 40, SSD1306_WHITE);

  display.display();
}

void drawChaosFace() {
  if (!oledReady) return;

  display.clearDisplay();

  display.drawCircle(64, 34, 24, SSD1306_WHITE);

  display.drawTriangle(43, 17, 50, 2, 58, 19, SSD1306_WHITE);
  display.drawTriangle(70, 19, 78, 2, 85, 17, SSD1306_WHITE);

  display.drawLine(48, 26, 60, 32, SSD1306_WHITE);
  display.drawLine(80, 26, 68, 32, SSD1306_WHITE);

  display.fillCircle(56, 34, 3, SSD1306_WHITE);
  display.fillCircle(72, 34, 3, SSD1306_WHITE);

  display.drawLine(52, 48, 76, 43, SSD1306_WHITE);

  display.drawTriangle(58, 46, 61, 53, 64, 45, SSD1306_WHITE);
  display.drawTriangle(68, 45, 71, 53, 74, 44, SSD1306_WHITE);

  display.display();
}
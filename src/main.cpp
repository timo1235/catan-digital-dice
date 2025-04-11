#include <Arduino.h>
#include <esp_sleep.h>
#include <TFT_eSPI.h> // Graphics and font library for ST7735 driver chip
#include <SPI.h>
#include <bitmaps.h>
#include <esp_adc_cal.h>
#include <Preferences.h>
#include <AceButton.h>
using namespace ace_button;

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;
esp_adc_cal_characteristics_t adc_chars;

AceButton button;
AceButton menuButton;

#define R1 51000.0
#define R2 51000.0

enum DiceMode
{
  REALISTIC,
  EQUAL
};
enum GameVariant
{
  BASE,
  CITIES_AND_KNIGHTS,
  TRADERS_AND_BARBARIANS,
};

enum DiceType
{
  NUMBER,
  COLOR,
  CASTLE
};

enum PowerSaveMode
{
  NEVER,
  AFTER_5_MIN,
  AFTER_10_MIN
};

// strukt for dices
struct Dice
{
  uint16_t color;
  uint16_t dotColor;
  uint8_t number;
  uint8_t diceSize;
  uint8_t dotSize;
  DiceType type;
};

// Default Values
const int twoDiceSize = 70;
const int threeDiceSize = 50;
const int twoDiceDotSize = twoDiceSize / 10;
RTC_DATA_ATTR Dice whiteDice = {TFT_WHITE, TFT_BLACK, 6, twoDiceSize, twoDiceDotSize, NUMBER};
RTC_DATA_ATTR Dice redDice = {TFT_RED, TFT_WHITE, 6, twoDiceSize, twoDiceDotSize, NUMBER};
RTC_DATA_ATTR Dice castleDice = {TFT_WHITE, TFT_BLACK, 6, threeDiceSize, 0, CASTLE};
RTC_DATA_ATTR Dice colorDice = {TFT_WHITE, TFT_BLACK, 6, threeDiceSize, threeDiceSize / 3, COLOR};
DiceMode diceMode = REALISTIC;
GameVariant gameVariant = BASE;
PowerSaveMode powerSaveMode = AFTER_10_MIN;
uint32_t lastBatteryCheck = 0;
uint16_t batteryCheckInterval = 10000; // 10 seconds
float_t batteryVoltage = 0;
uint8_t batteryPercentage = 0;

uint32_t lastButtonPress = 0;

// Array to store rolled dice statistics (indices 2-12)
RTC_DATA_ATTR uint16_t diceStatistics[13] = {0};

uint8_t menuPage = 0;
bool isInMenu = false;
bool isInStats = false;

// Forward reference to prevent Arduino compiler becoming confused.
void handleButton(AceButton *, uint8_t, uint8_t);
void displayStatus();
void drawDot(int x, int y, Dice dice);
void drawSingleDice(int x, int y, Dice dice);
int getSingleDiceNumber();
void setDiceSize(uint8_t size, Dice *dice);
void rollDice();
void displayStatistics();
void resetStatistics();
void animateRoll(Dice *dice1, Dice *dice2, Dice *extra);
void showDiceScreen();
void setTwoDiceNumbers(Dice *dice1, Dice *dice2, bool addToStatistics = true);
void setCorrectDiceSize();
void showMenuPage();
void drawMenuOption(int y, const char *label, const char *description, bool selected, bool drawLineBelow = true);
float readBatteryVoltage();
int voltageToPercentage(float voltage);
void drawBatteryIcon(int x, int y, uint8_t percentage);
void loadSettings();
void saveSettings();
void setMainButtonLed(bool state)
{
  digitalWrite(BUTTON_LED_PIN, state ? HIGH : LOW);
}
void setMenuButtonLed(bool state)
{
  digitalWrite(MENU_LED_PIN, state ? HIGH : LOW);
}
void enterDeepSleep()
{
  // digitalWrite(DISPLAY_VCC_PIN, LOW);
  // digitalWrite(DISPLAY_BL_PIN, LOW);
  // Wakeup-source
  esp_deep_sleep_enable_gpio_wakeup((1ULL << BUTTON_PIN), ESP_GPIO_WAKEUP_GPIO_LOW);
  esp_deep_sleep_start();
}

void setup(void)
{
  Serial.begin(115200);
  // Serial.end();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUTTON_LED_PIN, OUTPUT);
  pinMode(MENU_PIN, INPUT_PULLUP);
  pinMode(MENU_LED_PIN, OUTPUT);

  // Both buttos sharing the same config. Therefore we need to set the features etc only once.
  button.init((uint8_t)BUTTON_PIN, HIGH, 1);
  menuButton.init((uint8_t)MENU_PIN, HIGH, 2);
  button.setEventHandler(handleButton);
  button.getButtonConfig()->setFeature(AceButton::kEventClicked);
  button.getButtonConfig()->setFeature(AceButton::kEventLongPressed);
  button.getButtonConfig()->setClickDelay(500);

  // Battery state // ADC-Kalibrierung
  analogReadResolution(12); // 12-Bit-Resolution
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);
  batteryVoltage = readBatteryVoltage();
  batteryPercentage = voltageToPercentage(batteryVoltage);
  lastBatteryCheck = millis();

  // load settings
  loadSettings();

  // Display
  pinMode(DISPLAY_GND_PIN, OUTPUT);
  pinMode(DISPLAY_VCC_PIN, OUTPUT);
  digitalWrite(DISPLAY_GND_PIN, LOW);  // GND
  digitalWrite(DISPLAY_VCC_PIN, HIGH); // VCC
  pinMode(DISPLAY_BL_PIN, OUTPUT);
  digitalWrite(DISPLAY_BL_PIN, HIGH); // BL
  delay(200);                         // Wait for the display to power up
  tft.init();
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  setMenuButtonLed(1);
  setCorrectDiceSize();
  showDiceScreen();

  lastButtonPress = millis();
}

void loop()
{
  // rollDice();
  // delay(30000);
  button.check();
  menuButton.check();
  // Battery check
  if (millis() - lastBatteryCheck > batteryCheckInterval)
  {
    batteryVoltage = readBatteryVoltage();
    batteryPercentage = voltageToPercentage(batteryVoltage);
    lastBatteryCheck = millis();
  }
  // Power saving
  if ((powerSaveMode == AFTER_5_MIN && millis() - lastButtonPress > 5 * 60 * 1000) ||
      (powerSaveMode == AFTER_10_MIN && millis() - lastButtonPress > 10 * 60 * 1000))
  {
    delay(100);
    enterDeepSleep();
  }
}

void loadSettings()
{
  prefs.begin("dice-settings", true); // "true" = read-only mode
  diceMode = (DiceMode)prefs.getUChar("dicemode", REALISTIC);
  gameVariant = (GameVariant)prefs.getUChar("gamevariant", BASE);
  powerSaveMode = (PowerSaveMode)prefs.getUChar("powersave", AFTER_10_MIN);
  prefs.end();
}

void saveSettings()
{
  prefs.begin("dice-settings", false);
  prefs.putUChar("dicemode", (uint8_t)diceMode);
  prefs.putUChar("gamevariant", (uint8_t)gameVariant);
  prefs.putUChar("powersave", (uint8_t)powerSaveMode);
  prefs.end();
}

void setCorrectDiceSize()
{
  if (gameVariant == BASE)
  {
    setDiceSize(twoDiceSize, &whiteDice);
    setDiceSize(twoDiceSize, &redDice);
  }
  else if (gameVariant == TRADERS_AND_BARBARIANS || gameVariant == CITIES_AND_KNIGHTS)
  {
    setDiceSize(threeDiceSize, &whiteDice);
    setDiceSize(threeDiceSize, &redDice);
    setDiceSize(threeDiceSize, &colorDice);
    setDiceSize(threeDiceSize, &castleDice);
  }
}

// convert battery voltage to percentage
int voltageToPercentage(float voltage)
{
  if (voltage >= 4.05)
    return 100;
  if (voltage >= 4.00)
    return 90;
  if (voltage >= 3.90)
    return 80;
  if (voltage >= 3.80)
    return 70;
  if (voltage >= 3.70)
    return 50;
  if (voltage >= 3.60)
    return 30;
  if (voltage >= 3.50)
    return 20;
  if (voltage >= 3.40)
    return 10;
  return 0; // below 3.50V is considered empty
}

// Measure the battery voltage
float readBatteryVoltage()
{
  uint32_t raw = analogRead(BATTERY_PIN);
  uint32_t millivolts = esp_adc_cal_raw_to_voltage(raw, &adc_chars);

  float voltageADC = millivolts / 1000.0; // Millivolt zu Volt
  float batteryVoltage = voltageADC / (R2 / (R1 + R2));

  return batteryVoltage;
}

void drawBatteryIcon(int x, int y, uint8_t percentage)
{
  const int w = 20;             // Breite des Symbols
  const int h = 10;             // Höhe des Symbols
  const int levelWidth = w - 4; // Breite der Füllanzeige
  const int pinWidth = 2;

  // Gehäuse
  tft.drawRect(x, y, w, h, TFT_WHITE);
  tft.fillRect(x + w, y + 3, pinWidth, h - 6, TFT_WHITE); // kleiner "Pin" rechts

  // Füllung je nach Akkustand
  int fill = map(percentage, 0, 100, 0, levelWidth);
  uint16_t fillColor = TFT_GREEN;
  if (percentage <= 20)
    fillColor = TFT_RED;
  else if (percentage <= 50)
    fillColor = TFT_ORANGE;

  tft.fillRect(x + 2, y + 2, fill, h - 4, fillColor);
}

void showDiceScreen()
{
  tft.fillScreen(TFT_BLACK);
  displayStatus();

  if (gameVariant == BASE)
  {
    drawSingleDice(5, 30, whiteDice);
    drawSingleDice(85, 30, redDice);
  }
  else if (gameVariant == TRADERS_AND_BARBARIANS)
  {
    drawSingleDice(20, 20, whiteDice);
    drawSingleDice(95, 20, redDice);
    drawSingleDice(55, 75, colorDice);
  }
  else if (gameVariant == CITIES_AND_KNIGHTS)
  {
    drawSingleDice(20, 20, whiteDice);
    drawSingleDice(95, 20, redDice);
    drawSingleDice(55, 75, castleDice);
  }
  setMainButtonLed(1);
}

void animateRoll(Dice *dice1, Dice *dice2, Dice *extra)
{
  for (int i = 0; i < 10; i++)
  {
    tft.fillRect(0, 15, 260, 128 - 15, TFT_BLACK);

    int offsetX = random(-5, 5);
    int offsetY = random(-5, 5);

    setTwoDiceNumbers(dice1, dice2, false);
    if (extra != nullptr)
    {
      extra->number = getSingleDiceNumber();
    }

    if (gameVariant == BASE)
    {
      drawSingleDice(5 + offsetX, 30 + offsetY, *dice1);
      drawSingleDice(85 - offsetX, 30 - offsetY, *dice2);
    }
    else if (gameVariant == TRADERS_AND_BARBARIANS || gameVariant == CITIES_AND_KNIGHTS)
    {
      drawSingleDice(20 + offsetX, 20 + offsetY, *dice1);
      drawSingleDice(95 + offsetX, 20 + offsetY, *dice2);
      drawSingleDice(55 + offsetX, 75 + offsetY, *extra);
    }
    delay(100);
  }
}

void rollDice()
{
  if (gameVariant == BASE)
  {
    animateRoll(&whiteDice, &redDice, nullptr);
    setTwoDiceNumbers(&whiteDice, &redDice, true);
  }
  else if (gameVariant == TRADERS_AND_BARBARIANS)
  {
    animateRoll(&whiteDice, &redDice, &colorDice);
    setTwoDiceNumbers(&whiteDice, &redDice, true);
    colorDice.number = getSingleDiceNumber();
  }
  else if (gameVariant == CITIES_AND_KNIGHTS)
  {
    animateRoll(&whiteDice, &redDice, &castleDice);
    setTwoDiceNumbers(&whiteDice, &redDice, true);
    castleDice.number = getSingleDiceNumber();
  }

  showDiceScreen();
}

void handleButton(AceButton *button, uint8_t eventType, uint8_t /*buttonState*/)
{
  if (button->getId() == 1) // Main button
  {
    lastButtonPress = millis();
    if (eventType == AceButton::kEventClicked)
    {
      if (isInMenu)
      {
        switch (menuPage)
        {
        case 0: // Dice Mode
          diceMode = (diceMode == REALISTIC) ? EQUAL : REALISTIC;
          break;
        case 1: // Game Variant
          gameVariant = static_cast<GameVariant>((gameVariant + 1) % 3);
          setCorrectDiceSize();
          break;
        case 2: // Power saving
          powerSaveMode = static_cast<PowerSaveMode>((powerSaveMode + 1) % 3);
          break;
        }
        showMenuPage();
      }
      else if (isInStats)
      {
        isInStats = false;
        showDiceScreen();
      }
      else
      {
        setMainButtonLed(0);
        rollDice();
      }
    }
    else if (eventType == AceButton::kEventLongPressed)
    {
      if (!isInMenu)
        displayStatistics();
    }
  }
  else if (button->getId() == 2) // Menu button
  {
    lastButtonPress = millis();
    if (eventType == AceButton::kEventClicked)
    {
      if (!isInMenu)
      {
        isInMenu = true;
        menuPage = 0;
        showMenuPage();
      }
      else
      {
        menuPage++;
        if (menuPage > 2)
        {
          isInMenu = false;
          saveSettings();
          resetStatistics();
          showDiceScreen();
        }
        else
        {
          showMenuPage();
        }
      }
    }
    else if (eventType == AceButton::kEventLongPressed)
    {
      if (!isInMenu)
        resetStatistics();
    }
  }
}

void drawMenuOption(int y, const char *label, const char *description, bool selected, bool drawLineBelow)
{
  int x = 10;
  int radius = 4;
  tft.setTextSize(1);

  // Radio Button
  tft.drawCircle(x, y + 4, radius, TFT_WHITE);
  if (selected)
  {
    tft.fillCircle(x, y + 4, radius - 1, TFT_WHITE);
  }

  // Option Label
  tft.setCursor(x + 12, y);
  tft.setTextColor(TFT_WHITE);
  tft.print(label);

  // Beschreibung
  tft.setTextColor(TFT_SKYBLUE);
  const int maxCharsPerLine = 22;
  int descY = y + 12;
  int indentX = x + 12;

  size_t len = strlen(description);
  if (len > maxCharsPerLine)
  {
    // Finde letztes Leerzeichen vor Grenze
    int splitPos = maxCharsPerLine;
    for (int i = maxCharsPerLine; i >= 0; i--)
    {
      if (description[i] == ' ')
      {
        splitPos = i;
        break;
      }
    }

    char firstLine[splitPos + 1];
    strncpy(firstLine, description, splitPos);
    firstLine[splitPos] = '\0';

    const char *secondLine = description + splitPos + 1;

    tft.setCursor(indentX, descY);
    tft.print(firstLine);

    tft.setCursor(indentX, descY + 10);
    tft.print(secondLine);
    // Trennlinie darunter
    if (drawLineBelow)
    {
      tft.drawLine(x, y + 32, 160 - x, y + 32, TFT_DARKGREY);
    }
  }
  else
  {
    tft.setCursor(indentX, descY);
    tft.print(description);
    // Trennlinie darunter
    if (drawLineBelow)
    {
      tft.drawLine(x, y + 26, 160 - x, y + 26, TFT_DARKGREY);
    }
  }
}

void showMenuPage()
{
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN);
  tft.setCursor(10, 5);

  if (menuPage == 0)
  {
    tft.setTextFont(2);
    tft.println("== Dice Mode ==");
    tft.setTextFont(1);

    drawMenuOption(25, "Realistic",
                   "Realtistic roll with 2 dices",
                   diceMode == REALISTIC);
    drawMenuOption(65, "Equal Distribution",
                   "Every number has the same chance",
                   diceMode == EQUAL,
                   false);

    tft.setCursor(10, 100);
    tft.setTextColor(TFT_YELLOW);
  }

  else if (menuPage == 1)
  {
    tft.setTextFont(2);
    tft.println("== Game Variant ==");
    tft.setTextFont(1);

    drawMenuOption(25, "Base",
                   "2 number dices",
                   gameVariant == BASE);
    drawMenuOption(58, "Cities & Knights",
                   "2 number dices + 1 castle dice",
                   gameVariant == CITIES_AND_KNIGHTS);
    drawMenuOption(97, "Traders & Barbarians",
                   "2 number dices + 1 color dice",
                   gameVariant == TRADERS_AND_BARBARIANS,
                   false);

    tft.setCursor(10, 125);
    tft.setTextColor(TFT_YELLOW);
  }
  else if (menuPage == 2)
  {
    tft.setTextFont(2);
    tft.println("== Power Saving ==");
    tft.setTextFont(1);

    drawMenuOption(25, "5min",
                   "Afer 5 min",
                   powerSaveMode == AFTER_5_MIN);
    drawMenuOption(58, "10min",
                   "After 10min",
                   powerSaveMode == AFTER_10_MIN);
    drawMenuOption(97, "Never",
                   "no power saving",
                   powerSaveMode == NEVER,
                   false);

    tft.setCursor(10, 125);
    tft.setTextColor(TFT_YELLOW);
  }
}

void displayStatus()
{
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setCursor(5, 5);
  tft.print((diceMode == REALISTIC) ? "Realistic" : "Equal");
  tft.print(" | ");
  switch (gameVariant)
  {
  case CITIES_AND_KNIGHTS:
    tft.print("Cities");
    break;
  case TRADERS_AND_BARBARIANS:
    tft.print("Traders");
    break;

  default:
    tft.print("Base");
    break;
  }
  // tft.setCursor(135, 5);
  // tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  // tft.print(batteryPercentage);
  // tft.print("% ");
  drawBatteryIcon(135, 2, batteryPercentage);
}

void drawDot(int x, int y, Dice dice)
{
  tft.fillCircle(x, y, dice.dotSize, dice.dotColor);
}

void drawSingleDice(int x, int y, Dice dice)
{
  int radius = dice.diceSize / 5; // corner radius
  tft.fillRoundRect(x, y, dice.diceSize, dice.diceSize, radius, dice.color);

  if (dice.type == NUMBER)
  {
    int offset = dice.diceSize / 4;
    int cx = x + dice.diceSize / 2;
    int cy = y + dice.diceSize / 2;

    // Middle dot
    if (dice.number % 2 == 1)
      drawDot(cx, cy, dice);

    // Top-left and bottom-right dots
    if (dice.number > 1)
    {
      drawDot(x + offset, y + offset, dice);
      drawDot(x + dice.diceSize - offset, y + dice.diceSize - offset, dice);
    }

    // Top-right and bottom-left dots
    if (dice.number > 3)
    {
      drawDot(x + dice.diceSize - offset, y + offset, dice);
      drawDot(x + offset, y + dice.diceSize - offset, dice);
    }

    // Middle dots left and right for number 6
    if (dice.number == 6)
    {
      drawDot(x + offset, cy, dice);
      drawDot(x + dice.diceSize - offset, cy, dice);
    }
  }
  else if (dice.type == COLOR)
  {
    uint16_t colors[7] = {TFT_WHITE, TFT_GREEN, TFT_GREEN, TFT_GOLD, TFT_GOLD, TFT_PURPLE, TFT_PURPLE};
    int cx = x + dice.diceSize / 2;
    int cy = y + dice.diceSize / 2;
    tft.fillCircle(cx, cy, dice.dotSize, colors[dice.number]);
  }
  else if (dice.type == CASTLE)
  {
    int bmpSize = 40;
    int cx = x + dice.diceSize / 2 - bmpSize / 2;
    int cy = y + dice.diceSize / 2 - bmpSize / 2;
    if (dice.number == 1 || dice.number == 2 || dice.number == 3)
    {
      tft.drawBitmap(cx, cy, pirateShip, bmpSize, bmpSize, TFT_WHITE, TFT_BLACK);
    }
    else if (dice.number == 4)
    {
      tft.drawBitmap(cx, cy, burg, bmpSize, bmpSize, TFT_WHITE, TFT_BLUE);
    }
    else if (dice.number == 5)
    {
      tft.drawBitmap(cx, cy, burg, bmpSize, bmpSize, TFT_WHITE, TFT_GOLD);
    }
    else if (dice.number == 6)
    {
      tft.drawBitmap(cx, cy, burg, bmpSize, bmpSize, TFT_WHITE, TFT_GREEN);
    }
  }
}

int getSingleDiceNumber()
{
  return random(1, 7);
}

void setTwoDiceNumbers(Dice *dice1, Dice *dice2, bool addToStatistics)
{
  if (diceMode == EQUAL)
  {
    int sum = random(2, 13);
    int number1 = 0;
    int number2 = 0;

    if (sum == 2)
    {
      number1 = number2 = 1;
    }
    else if (sum == 12)
    {
      number1 = number2 = 6;
    }
    else
    {
      // Ensure both dice are valid
      do
      {
        number1 = random(1, 7);
        number2 = sum - number1;
      } while (number2 < 1 || number2 > 6);
    }

    dice1->number = number1;
    dice2->number = number2;

    // Update statistics
    if (addToStatistics)
    {
      diceStatistics[sum]++;
    }
  }
  else if (diceMode == REALISTIC)
  {
    int number1 = getSingleDiceNumber();
    int number2 = getSingleDiceNumber();

    dice1->number = number1;
    dice2->number = number2;
    // Update the statistics
    if (addToStatistics)
    {
      diceStatistics[number1 + number2]++;
    }
  }
}

void setDiceSize(uint8_t size, Dice *dice)
{
  dice->diceSize = size;
  if (dice->type == NUMBER)
  {
    dice->dotSize = size / 10;
  }
  else if (dice->type == COLOR)
  {
    dice->dotSize = size / 3;
  }
}

void displayStatistics()
{
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(5, 5);
  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);
  tft.println("Dice Statistics (2-12):");

  for (int i = 2; i <= 12; i++)
  {
    tft.setCursor(5, 10 + (i - 1) * 10);
    tft.printf("%2d: %d\n", i, diceStatistics[i]);
  }
  isInStats = true;
}

void resetStatistics()
{
  for (int i = 2; i <= 12; i++)
  {
    diceStatistics[i] = 0;
  }
}
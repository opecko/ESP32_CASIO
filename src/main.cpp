// ESP32 CASIO
#include "esp_attr.h"
#include "freertos/portmacro.h"
#include "time.h"
#include <Arduino.h>
#include <DHT11.h>
#include <U8g2lib.h>
#include <WiFi.h>

// ========================================================
// PINY
// ========================================================
#define startPin 19 // player 2
#define resetPin 27 // player 1
#define menuPin 26
#define backlightBtn 23
#define beepPin 25
#define backlightLED 2
#define dhtPin 13

#define bluPin 19
#define redPin 27

// ========================================================
// FONTS
// ========================================================
#define font u8g2_font_neuecraft_tr
#define textFont u8g2_font_pxplusibmvga8_tf
#define hodinyFont u8g2_font_logisoso24_tr
#define titleFont u8g2_font_luRS12_te
#define stopwatchFont u8g2_font_fub17_tn

// GAMING FONTS

// main text
#define fontMain u8g2_font_neuecraft_tr
// title text
#define fontTitle u8g2_font_logisoso16_tf
// teeny tiny font
#define fontTiny u8g2_font_5x8_tr
// big numbers (rolling dice)
#define fontBigNum u8g2_font_fub20_tr
// winner font ofc
#define fontWinner u8g2_font_7x14B_mf

// ========================================================
// DISPLAY
// ========================================================
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void drawCentered(int y, const char *text) {
  int x = (128 - u8g2.getStrWidth(text)) / 2;
  u8g2.drawUTF8(x, y, text);
}

// ========================================================
// SETTINGS
// ========================================================
int pageIndex = 1;

int alarmBeepingLength = 350;
int alarmBeepingCycles = 3;
int alarmBeepDelay = 150;

// ========================================================
// WIFI + NTP
// ========================================================
const char *ssid = "Doma";
const char *password = "Heslo_ti_nereknu!";

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;
const int daylightOffset_sec = 3600;
// ========================================================

// ========================================================
// DHT
// ========================================================

/*
!!!!!!!!!!! ATTENTION !!!!!!!!!!!

S goes into dhtPin
MIDDLE goes into 3V3
PRAVY goes to GROUND

*/

DHT11 dht11(dhtPin);

int temp = 0;
int hum = 0;

// ========================================================
// DATE
// ========================================================
const char *months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

const char *daySuffixes[] = {"th", "st", "nd", "rd"};

bool allowMenuButtonSwitching = true;

// ========================================================
// STOPWATCH
// ========================================================
bool running = false;
bool paused = false;

unsigned long startedAt = 0;
unsigned long pausedAt = 0;
unsigned long ms = 0;
unsigned long ms2Cifry = 0;
unsigned long hrs = 0;
unsigned long mins = 0;
unsigned long secs = 0;

// ========================================================
// ALARM
// ========================================================
bool alarmSet = false;
bool alarmEditing = false;
bool alarmDissmised = false;
bool alarmOn = false;

int alarmHour = 0;
int alarmMinute = 0;
int alarmEditState = 0;

unsigned long lastBlinkTime = 0;
bool blinkState = true;

// =========================================================
// GAMING VARIABLES
// =========================================================
bool bluRolled = false, redRolled = false;
int bluNum = 0, redNum = 0;
int bluRoundsWon = 0, redRoundsWon = 0;
int roundNum = 1;

bool bluIsRolling = false, redIsRolling = false;
int rollTime = 500;
unsigned long bluRollStarted = 0, redRollStarted = 0;

bool gameActive = false;

int totalGames = 0;
int bluGamesWon = 0;
int redGamesWon = 0;
int drawGameCount = 0;

/*
 gameState:
 0 = main screen (tohle tam bude 99% času lol)
 1 = GAMING
 2 = konec hry
*/
int gameState = 0;

// debounce
unsigned long lastP1Press = 0, lastP2Press = 0;
const unsigned long debounceTime = 200;

bool tiebreaker = false;

// ========================================================
// EDGE DETECTION
// ========================================================
bool lastStartButton = HIGH;
bool lastResetButton = HIGH;
bool lastMenuButton = HIGH;

// ========================================================
// BACKLIGHT ISR AND TASK
// ========================================================
TaskHandle_t backlightTaskHandle = NULL;

void IRAM_ATTR onBacklight() {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(backlightTaskHandle, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void backlightTask(void *param) {
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    digitalWrite(backlightLED, HIGH);
    vTaskDelay(pdMS_TO_TICKS(3000));
    digitalWrite(backlightLED, LOW);
  }
}
// ========================================================
// FUNCTION DECLARATIONS
// ========================================================
void pageDrawer();
void drawClock();
void drawStopwatch();
void drawAlarmSetup();
void alarmAlarming();
void drawWeather();
void drawGameLaunch();

void rollDice(bool &rolled, bool &isRolling, int &num, unsigned long &rollStart,
              int pin);
void gameLoop();
void finishRound();
void resetGame();
void updateDisp();
void drawRoundWinner(int p);
void gameStartScreen();

// ========================================================
// SETUP
// ========================================================
void setup() {
  Serial.begin(9600);

  pinMode(backlightBtn, INPUT_PULLUP);
  pinMode(backlightLED, OUTPUT);
  xTaskCreatePinnedToCore(backlightTask, "backlightTask", 2048, NULL, 1,
                          &backlightTaskHandle, 0);
  attachInterrupt(digitalPinToInterrupt(backlightBtn), onBacklight, FALLING);

  u8g2.begin();
  u8g2.setFont(font);
  pinMode(startPin, INPUT_PULLUP);
  pinMode(resetPin, INPUT_PULLUP);
  pinMode(menuPin, INPUT_PULLUP);
  pinMode(beepPin, OUTPUT);
  randomSeed(esp_random());
  // loading screen
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_fub17_tf);
  u8g2.drawStr(0, 22, "ESP32");
  u8g2.drawStr(0, 42, "Clock");
  u8g2.setFont(textFont);
  u8g2.drawStr(0, 58, "Connecting...");
  u8g2.sendBuffer();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
  }
  WiFi.mode(WIFI_STA);
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(500);
  }
}

// ========================================================
// LOOP
// ========================================================
void loop() {

  if (!gameActive) {
    u8g2.clearBuffer();
    int menuState = digitalRead(menuPin);
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
      return;

    // --- ALARM LOGIKA ---
    if (alarmSet && alarmOn && !alarmDissmised &&
        alarmHour == timeinfo.tm_hour && alarmMinute == timeinfo.tm_min &&
        !alarmEditing) {
      alarmAlarming();             //  draws alarm to buffer and stops so we don't draw pages
      u8g2.sendBuffer();
      delay(50);
      return;                      // SKIP pageDrawer and other things
    }

    if (alarmSet && alarmDissmised && alarmHour != timeinfo.tm_hour &&
        alarmMinute != timeinfo.tm_min) {
      alarmDissmised = false;
    }

    if (allowMenuButtonSwitching && menuState == LOW) {
      pageIndex++;
      if (pageIndex >= 6)
        pageIndex = 1;
      delay(350);
    }

    pageDrawer();

    // u8g2.drawVLine(63, 0, 64);
    // The line above this comment is legacy developing help, used to center text before drawCentered() was a thing. I'm still gonna keep it in here because why not

    u8g2.sendBuffer();
    delay(50);
  } else {

    if (roundNum <= 3)
      updateDisp();
    if (gameState != 0)
      gameLoop();
  }
}


// ========================================================
// PAGE DRAWER
// ========================================================
void pageDrawer() {
  if (pageIndex == 1)
    drawClock();
  if (pageIndex == 2)
    drawStopwatch();
  if (pageIndex == 3)
    drawAlarmSetup();
  if (pageIndex == 4)
    drawWeather();
  if (pageIndex == 5)
    drawGameLaunch();

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
    return;

  if (pageIndex != 1) {
    char hT[8];
    sprintf(hT, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    u8g2.setFont(textFont);
    u8g2.drawStr(84, 12, hT);
  }
}

int daySuffix(int day) {
  if (day >= 11 && day <= 13)
    return 0; // 11th, 12th, 13th, je vyjímka

  switch (day % 10) {
  case 1:
    return 1; // st
  case 2:
    return 2; // nd
  case 3:
    return 3; // rd
  default:
    return 0; // th
  }
}

// ========================================================
// HODINY
// ========================================================
void drawClock() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo))
    return;

  // Header
  u8g2.setFont(titleFont);
  u8g2.drawStr(0, 12, "Clock");
  u8g2.drawHLine(0, 14, 128);

  // Čas
  char cas[16];
  snprintf(cas, sizeof(cas), "%02d:%02d:%02d", timeinfo.tm_hour,
           timeinfo.tm_min, timeinfo.tm_sec);

  u8g2.setFont(hodinyFont);
  u8g2.drawStr(6, 44, cas);

  // Datum
  char datum[32];
  snprintf(datum, sizeof(datum), "%d%s %s %d", timeinfo.tm_mday,
           daySuffixes[daySuffix(timeinfo.tm_mday)], months[timeinfo.tm_mon],
           timeinfo.tm_year + 1900);

  u8g2.setFont(textFont);
  drawCentered(62, datum);
}

// ========================================================
// STOPKY
// ========================================================
void drawStopwatch() {
  u8g2.setFont(titleFont);
  u8g2.drawStr(0, 12, "Stopwatch");
  u8g2.drawHLine(0, 14, 128);

  bool startButton = digitalRead(startPin);
  bool resetButton = digitalRead(resetPin);

  if (lastStartButton == HIGH && startButton == LOW) {
    if (!running) {
      running = true;
      paused = false;
      startedAt = millis();
    } else {
      paused = !paused;
      if (paused)
        pausedAt = millis() - startedAt;
      else
        startedAt = millis() - pausedAt;
    }
  }
  lastStartButton = startButton;

  if (lastResetButton == HIGH && resetButton == LOW) {
    running = false;
    paused = false;
    pausedAt = 0;
    ms = 0;
  }
  lastResetButton = resetButton;

  if (running && !paused)
    ms = millis() - startedAt;
  else if (paused)
    ms = pausedAt;

  hrs = ms / 3600000;
  mins = (ms % 3600000) / 60000;
  secs = (ms % 60000) / 1000;
  ms2Cifry = (ms % 1000) / 10;

  char t[32];
  sprintf(t, "%02lu:%02lu:%02lu:%02lu", hrs, mins, secs, ms2Cifry);

  u8g2.setFont(stopwatchFont);
  u8g2.drawStr(2, 46, t);
}
// ========================================================
// NASTAVENÍ BUDÍKU
// ========================================================
void drawAlarmSetup() {
  bool startButton = digitalRead(startPin);
  bool resetButton = digitalRead(resetPin);
  bool menuButton = digitalRead(menuPin);

  // edge detekce
  bool startEdge = (lastStartButton == HIGH && startButton == LOW);
  bool resetEdge = (lastResetButton == HIGH && resetButton == LOW);
  bool menuEdge = (lastMenuButton == HIGH && menuButton == LOW);

  lastStartButton = startButton;
  lastResetButton = resetButton;
  lastMenuButton = menuButton;

  // ===== HEADER =====
  u8g2.setFont(titleFont);
  u8g2.drawStr(0, 12, "Alarm");
  u8g2.drawHLine(0, 14, 128);

  u8g2.setFont(textFont);

  // =====================================
  // NOT EDITING - Info o budíku
  // =====================================
  if (!alarmEditing) {

    if (alarmOn)
      drawCentered(58, "On");
    else if (alarmSet)
      drawCentered(58, "Off");
    else
      drawCentered(58, "Not set");

    char t[6];
    sprintf(t, "%02d:%02d", alarmHour, alarmMinute);
    u8g2.setFont(hodinyFont);
    u8g2.drawStr(28, 44, t);
    u8g2.setFont(textFont);

    if (startEdge) {
      alarmEditing = true;
      alarmEditState = 0;
      allowMenuButtonSwitching = false;
    }

    if (resetEdge && !alarmOn)
      alarmOn = true;
    else if (resetEdge && alarmOn)
      alarmOn = false;

    return;
  }

  // Blikání podtržítka
  if (millis() - lastBlinkTime > 250) {
    blinkState = !blinkState;
    lastBlinkTime = millis();
  }

  // =====================================
  // EDITING - Nastavení budíku
  // =====================================
  char H1 = '0' + (alarmHour / 10);
  char H2 = '0' + (alarmHour % 10);
  char M1 = '0' + (alarmMinute / 10);
  char M2 = '0' + (alarmMinute % 10);

  if (blinkState) {
    switch (alarmEditState) {
    case 0:
      H1 = '_';
      break;
    case 1:
      H2 = '_';
      break;
    case 2:
      M1 = '_';
      break;
    case 3:
      M2 = '_';
      break;
    }
  }

  char T[6];
  sprintf(T, "%c%c:%c%c", H1, H2, M1, M2);

  u8g2.setFont(hodinyFont);
  u8g2.drawStr(28, 44, T);

  u8g2.setFont(textFont);
  drawCentered(58, "Editing...");

  // =================================
  // RESET - zvýšení hodnoty
  // =================================
  if (resetEdge) {
    switch (alarmEditState) {
    case 0:
      alarmHour += 10;
      if (alarmHour > 20)
        alarmHour = 0;
      if ((alarmHour / 10 == 2) && (alarmHour % 10 > 4))
        alarmHour = 24;
      break;

    case 1: {
      int tens = alarmHour / 10;
      int ones = alarmHour % 10;
      ones++;
      if (tens == 2 && ones > 4)
        ones = 0;
      if (tens < 2 && ones > 9)
        ones = 0;
      alarmHour = tens * 10 + ones;
      break;
    }

    case 2: {
      int tens = alarmMinute / 10;
      int ones = alarmMinute % 10;
      tens++;
      if (tens > 5)
        tens = 0;
      alarmMinute = tens * 10 + ones;
      break;
    }

    case 3: {
      int tens = alarmMinute / 10;
      int ones = alarmMinute % 10;
      ones++;
      if (ones > 9)
        ones = 0;
      alarmMinute = tens * 10 + ones;
      break;
    }
    }
  }

  // =====================================
  // START - další číslice
  // =====================================
  if (startEdge) {
    alarmEditState++;
    if (alarmEditState > 3)
      alarmEditState = 0;
  }

  // =====================================
  // MENU - uložit a konec
  // =====================================
  if (menuEdge) {
    alarmEditing = false;
    allowMenuButtonSwitching = true;
    alarmSet = true;

    u8g2.setDrawColor(0);
    u8g2.drawBox(0, 16, 128, 47);
    u8g2.setDrawColor(1);

    H1 = '0' + (alarmHour / 10);
    H2 = '0' + (alarmHour % 10);
    M1 = '0' + (alarmMinute / 10);
    M2 = '0' + (alarmMinute % 10);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
      return;

    char hT[8];
    sprintf(hT, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    u8g2.setFont(textFont);
    u8g2.drawStr(82, 12, hT);

    char T[6];
    sprintf(T, "%c%c:%c%c", H1, H2, M1, M2);

    u8g2.setFont(hodinyFont);
    u8g2.drawStr(28, 44, T);

    u8g2.setFont(textFont);
    drawCentered(58, "Alarm set!");
    u8g2.sendBuffer();
    delay(1000);
  }
}

// ========================================================
// BUDÍK – ALARM
// ========================================================
void alarmAlarming() {
  u8g2.clearBuffer();
  static uint8_t phase = 255;
  static unsigned long phaseStart = 0;
  static int beepCycle = 0;

  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(500);
  }

  if (digitalRead(startPin) == LOW) {
    digitalWrite(beepPin, LOW);
    alarmDissmised = true;
    pageIndex = 1;
    phase = 255;
    return;
  }

  if (phase == 255) {
    digitalWrite(beepPin, HIGH);
    phaseStart = millis();
    beepCycle = 0;
    phase = 0;
  }

  u8g2.setFont(titleFont);
  u8g2.drawStr(0, 12, "Alarm");
  u8g2.drawHLine(0, 14, 128);
  u8g2.setFont(font);

  drawCentered(52, "Good morning!");
  char timeBuf[6];
  sprintf(timeBuf, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);

  u8g2.drawStr(6, 44, timeBuf);

  unsigned long now = millis();

  switch (phase) {
  case 0: // Probíhá píp
    if (now - phaseStart >= (unsigned long)alarmBeepingLength) {
      digitalWrite(beepPin, LOW);
      phaseStart = now;
      phase = 1;
    }
    break;

  case 1: // Pauza mezi pípy
    if (now - phaseStart >= (unsigned long)alarmBeepDelay) {
      beepCycle++;
      if (beepCycle >= alarmBeepingCycles) {
        beepCycle = 0;
        phaseStart = now;
        phase = 2;
      } else {
        digitalWrite(beepPin, HIGH);
        phaseStart = now;
        phase = 0;
      }
    }
    break;

  case 2: // Dlouhá pauza mezi skupinami
    if (now - phaseStart >= (unsigned long)alarmBeepDelay * 3) {
      digitalWrite(beepPin, HIGH);
      phaseStart = now;
      phase = 0;
    }
    break;
  }
}

// ==================
//  TEPLOMĚR
// ==================

void drawWeather() {
  int result = dht11.readTemperatureHumidity(temp, hum);

  if (result == 0) {

    u8g2.setFont(titleFont);
    u8g2.drawStr(0, 12, "Temp");
    u8g2.drawHLine(0, 14, 128);

    char t[12];
    sprintf(t, "%02d C", temp);

    u8g2.setFont(hodinyFont);
    u8g2.drawStr(32, 44, t);

    char h[8];
    sprintf(h, "%02d%%", hum);

    u8g2.setFont(textFont);
    u8g2.drawStr(52, 58, h);
  } else {
    u8g2.drawStr(28, 44, "--.- C");
    Serial.println(DHT11::getErrorString(result));
  }
}

// ===========================
// SPUŠTĚNÍ HRY
// ==========================
void drawGameLaunch() {
  u8g2.drawHLine(0, 14, 128);
  u8g2.setFont(fontTitle);
  drawCentered(33, "DICE THROW");
  u8g2.drawHLine(8, 35, 110);
  u8g2.setFont(fontMain);
  drawCentered(48, "Press button");
  drawCentered(60, "to start!");

  bool startEdge = (lastStartButton == HIGH && digitalRead(startPin) == LOW);
  bool resetEdge = (lastResetButton == HIGH && digitalRead(resetPin) == LOW);

  lastStartButton = digitalRead(startPin);
  lastResetButton = digitalRead(resetPin);

  if (startEdge || resetEdge)
    gameActive = true;
}

// ======================================================================================================================================================

// ==================================================
//                 HOD KOSTKOU
// ==================================================

// ==================================
// ROLL LOGIKA (modrý i červený)
// ==================================
void rollDice(bool &rolled, bool &isRolling, int &num, unsigned long &rollStart,
              int pin) {
  unsigned long now = millis();
  if (!rolled && !isRolling && digitalRead(pin) == LOW) {
    isRolling = true;
    rollStart = now;
  }
  if (isRolling) {
    if (now - rollStart < rollTime)
      num = random(1, 7);
    else {
      isRolling = false;
      rolled = true;
      num = random(1, 7);
    }
  }
}

// ==================================
// GAME LOOP
// ==================================
void gameLoop() {
  unsigned long now = millis();

  if (roundNum > 3 && bluRoundsWon == redRoundsWon) {
    tiebreaker = true;
    updateDisp();
  } else if (roundNum > 3) {
    gameState = 2;
    updateDisp();
    totalGames++;
    resetGame();
    return;
  }

  rollDice(bluRolled, bluIsRolling, bluNum, bluRollStarted, bluPin);
  rollDice(redRolled, redIsRolling, redNum, redRollStarted, redPin);

  if (bluRolled && redRolled)
    finishRound();
}

// ==================================
// KONEC KOLA
// ==================================
void finishRound() {
  updateDisp();
  if (bluNum > redNum) {
    bluRoundsWon++;
    drawRoundWinner(1);
  } else if (redNum > bluNum) {
    redRoundsWon++;
    drawRoundWinner(2);
  } else
    drawRoundWinner(0);

  bluRolled = redRolled = bluNum = redNum = 0;
  roundNum++;
}

// ==================================
// RESET HRY
// ==================================
void resetGame() {
  roundNum = 1;
  bluRoundsWon = redRoundsWon = 0;
  gameActive = false;
  tiebreaker = false;
}

// ==================================
// UPDATE DISPLAY
// ==================================
void updateDisp() {
  u8g2.clearBuffer();
  char scoreBuff[8];

  switch (gameState) {
  case 0:
    gameStartScreen();
    break;

  case 1: {
    if (tiebreaker) {
      sprintf(scoreBuff, "%d:%d", redRoundsWon, bluRoundsWon);
      u8g2.setFont(fontMain);
      u8g2.drawStr(0, 12, "Tiebreaker");
      u8g2.drawStr(105, 14, scoreBuff);
      u8g2.drawHLine(0, 16, 128);
    } else {
      char buff[12];
      sprintf(buff, "Round %d/3", roundNum);
      sprintf(scoreBuff, "%d:%d", redRoundsWon, bluRoundsWon);
      u8g2.setFont(fontMain);
      u8g2.drawStr(0, 12, buff);
      u8g2.drawStr(105, 14, scoreBuff);
      u8g2.drawHLine(0, 16, 128);
    }

    // MODRÝ
    u8g2.drawFrame(0, 20, 62, 44);
    u8g2.setFont(fontTiny);
    u8g2.drawStr(86, 30, "BLUE");
    u8g2.setFont(fontBigNum);
    char b1[4];
    u8g2.drawStr(bluIsRolling ? random(86, 91) : 88,
                 bluIsRolling ? random(54, 61) : 56, itoa(bluNum, b1, 10));
    // ČERVENÝ
    u8g2.drawFrame(66, 20, 62, 44);
    u8g2.setFont(fontTiny);
    u8g2.drawStr(22, 30, "RED");
    u8g2.setFont(fontBigNum);
    char b2[4];
    u8g2.drawStr(redIsRolling ? random(20, 25) : 22,
                 redIsRolling ? random(54, 61) : 56, itoa(redNum, b2, 10));

    break;
  }

  case 2: {
    // KONEC HRY
    for (int y = 32; y >= 20; y--) {
      u8g2.clearBuffer();
      u8g2.setFont(fontTitle);
      drawCentered(y, "FINISH!");
      u8g2.sendBuffer();
      delay(20);
    }
    u8g2.clearBuffer();
    u8g2.setFont(fontTitle);
    drawCentered(20, "FINISH!");
    u8g2.drawHLine(0, 22, 128);
    u8g2.setFont(fontWinner);
    if (bluRoundsWon > redRoundsWon) {
      drawCentered(36, "BLUE WON!");
      bluGamesWon++;
    } else if (redRoundsWon > bluRoundsWon) {
      drawCentered(36, "RED WON!");
      redGamesWon++;
    } else {
      u8g2.drawStr(40, 36, "TIE!");
      drawGameCount++;
    }
    u8g2.drawFrame(18, 40, 88, 24);
    sprintf(scoreBuff, "%d : %d", redRoundsWon, bluRoundsWon);
    u8g2.setFont(fontBigNum);
    u8g2.drawStr(34, 62, scoreBuff);
    u8g2.sendBuffer();
    delay(2500);
    gameState = 0;
    break;
  }
  }
  u8g2.sendBuffer();
}

// ==================================
// VÝPIS VÍTĚZE KOLA
// ==================================
void drawRoundWinner(int p) {
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, 0, 128, 16);
  u8g2.setDrawColor(1);
  u8g2.setFont(fontWinner);
  if (p == 1)
    drawCentered(12, "BLUE WON ROUND");
  else if (p == 2)
    drawCentered(12, "RED WON ROUND");
  else
    drawCentered(12, "TIE!");
  u8g2.sendBuffer();
  delay(1500);
}

// ==================================
// STARTOVNÍ OBRAZOVKA
// ==================================
void gameStartScreen() {
  u8g2.setFont(fontTitle);
  u8g2.setDrawColor(0);
  u8g2.drawBox(0, 27, 128, 37);
  u8g2.setDrawColor(1);
  u8g2.drawStr(18, 56, "Start!");
  u8g2.sendBuffer();
  gameState = 1;
  delay(1000);
}

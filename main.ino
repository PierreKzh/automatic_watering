#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <RTClib.h>

// ── I2C ────────────────────────────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 20, 4);
RTC_DS3231 rtc;

// ── IO ─────────────────────────────────────────────────────────────────────
const int ledOnPin        = 2;
const int ledWateringPin  = 3;
const int relayPumpPin[]  = {4, 5, 6};  // Pump 1, 2, 3
const int buttonNextPin   = 8;
const int buttonSelectPin = 9;

// ── Constants ──────────────────────────────────────────────────────────────
const int           NUM_PUMPS        = 3;
const int           delayButton      = 300;
const int           manualDelay      = 5000;   // hold duration to enter manual mode (ms)
const unsigned long numberBlinkDelay = 500;
const unsigned long ledBlinkDelay    = 500;
const unsigned long standbyDelay     = 300000UL; // screen off after 5 min idle

// ── General state ──────────────────────────────────────────────────────────
int  window            = 0;
int  menuIndex         = 0;
int  selectedPump      = 0;  // pump currently being configured
int  manualPump        = 0;  // pump selected in manual mode
int  lastWindowManual  = 0;  // window to return to after leaving manual mode
int  lastWindowStandby = 0;  // window to restore after waking from standby

unsigned long lastTimeButtonPressed = 0;
unsigned long manualStartTime       = 0;
unsigned long lastNumberBlink       = 0;
unsigned long lastLedBlink          = 0;
unsigned long checkStandbyStartTime = 0;

bool numberBlink           = false;
bool ledBlink              = false;
bool isNumberChanging      = false;
bool isFirstNumberChanging = true;
bool isManual              = false;
bool isStandbyTimer        = false;
bool isStandby             = false;

// ── Per-pump state ─────────────────────────────────────────────────────────
unsigned long wateringStartTime[NUM_PUMPS] = {0, 0, 0};
bool isWatering[NUM_PUMPS]        = {false, false, false};
bool isAutoEnabledPump[NUM_PUMPS] = {false, false, false};

// ── Persistent schedule data ───────────────────────────────────────────────
struct WateringTime {
  int hour     = 0;
  int minute   = 0;
  int duration = 10;
};

WateringTime myWatering[NUM_PUMPS];
WateringTime tempWatering;


// ═══════════════════════════════════════════════════════════════════════════
//  SETUP / LOOP
// ═══════════════════════════════════════════════════════════════════════════

void setup() {
  pinMode(ledOnPin,       OUTPUT);
  pinMode(ledWateringPin, OUTPUT);
  for (int i = 0; i < NUM_PUMPS; i++) {
    pinMode(relayPumpPin[i], OUTPUT);
    EEPROM.get(i * (int)sizeof(WateringTime), myWatering[i]);  // restore schedule from flash
  }
  pinMode(buttonNextPin,   INPUT_PULLUP);
  pinMode(buttonSelectPin, INPUT_PULLUP);

  rtc.begin();
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));  // uncomment once to set clock

  lcd.init();
  lcd.backlight();
}

void loop() {
  digitalWrite(ledOnPin, HIGH);

  check_plants_watering();
  window = check_enable_manual(window);  // must run before standby check
  window = check_standby(window);

  switch (window) {
    case 0: window = Window0(); refresh_window_if_changed(0); break;
    case 1: window = Window1(); refresh_window_if_changed(1); break;
    case 2: window = Window2(); refresh_window_if_changed(2); break;
    case 3: window = Window3(); refresh_window_if_changed(3); break;
    case 4: window = Window4(); refresh_window_if_changed(4); break;
    case 5: window = Window5(); refresh_window_if_changed(5); break;
    case 6: break;  // standby — nothing to render
  }
}


// ═══════════════════════════════════════════════════════════════════════════
//  UTILITIES
// ═══════════════════════════════════════════════════════════════════════════

bool is_elapsed_time(unsigned long start, unsigned long duration) {
  return (millis() - start) >= duration;
}

// Returns true once per press, with debounce guard
bool is_button_pressed(int pin) {
  if (digitalRead(pin) == LOW && is_elapsed_time(lastTimeButtonPressed, delayButton)) {
    lastTimeButtonPressed = millis();
    return true;
  }
  return false;
}

void lcd_print_padding(int n) {
  for (int i = 0; i < n; i++) lcd.print(' ');
}

// True if at least one pump relay is currently active
bool any_pump_watering() {
  for (int i = 0; i < NUM_PUMPS; i++) if (isWatering[i]) return true;
  return false;
}

// Clear display and reset shared edit state on window transition
void refresh_window_if_changed(int currentWindow) {
  if (window != currentWindow) {
    lcd.clear();
    menuIndex             = 0;
    isNumberChanging      = false;
    isFirstNumberChanging = true;
  }
}


// ═══════════════════════════════════════════════════════════════════════════
//  MENU SELECTORS
// ═══════════════════════════════════════════════════════════════════════════

// 2-option menu on lines [startRow] and [startRow+1]
void create_menu_selector(int startRow, String opt1, String opt2) {
  lcd.setCursor(0, startRow);
  lcd.print(menuIndex == 0 ? "> " : "  ");
  lcd.print(opt1);
  lcd_print_padding(18 - opt1.length());

  lcd.setCursor(0, startRow + 1);
  lcd.print(menuIndex == 1 ? "> " : "  ");
  lcd.print(opt2);
  lcd_print_padding(18 - opt2.length());

  if (is_button_pressed(buttonNextPin))
    menuIndex = (menuIndex + 1) % 2;
}

// 3-option menu on lines [startRow] to [startRow+2]
void create_menu_selector_3(int startRow, String opt1, String opt2, String opt3) {
  String opts[3] = {opt1, opt2, opt3};
  for (int i = 0; i < 3; i++) {
    lcd.setCursor(0, startRow + i);
    lcd.print(menuIndex == i ? "> " : "  ");
    lcd.print(opts[i]);
    lcd_print_padding(18 - opts[i].length());
  }
  if (is_button_pressed(buttonNextPin))
    menuIndex = (menuIndex + 1) % 3;
}


// ═══════════════════════════════════════════════════════════════════════════
//  NUMBER EDITING
// ═══════════════════════════════════════════════════════════════════════════

// Blink the displayed value to signal it is being edited
void blink_number(char *number, int col, int row) {
  if (is_elapsed_time(lastNumberBlink, numberBlinkDelay)) {
    lastNumberBlink = millis();
    numberBlink = !numberBlink;
    lcd.setCursor(col, row);
    lcd.print(numberBlink ? number : "  ");
  }
}

// Increment the value on buttonNext press and update the display in place
void change_displayed_number(char *number, int col, int row, int *ptr, int maxVal) {
  blink_number(number, col, row);
  if (is_button_pressed(buttonNextPin)) {
    if (++(*ptr) >= maxVal) *ptr = 0;
    sprintf(number, "%02d", *ptr);
    lcd.setCursor(col, row);
    lcd.print(number);
  }
}


// ═══════════════════════════════════════════════════════════════════════════
//  PUMP CONTROL
// ═══════════════════════════════════════════════════════════════════════════

void enable_pump(int i) {
  isWatering[i] = true;
  digitalWrite(relayPumpPin[i], HIGH);
}

void disable_pump(int i) {
  isWatering[i]        = false;
  isAutoEnabledPump[i] = false;
  digitalWrite(relayPumpPin[i], LOW);
  // Turn off the watering LED only when all pumps have stopped
  if (!any_pump_watering()) digitalWrite(ledWateringPin, LOW);
}

void check_plants_watering() {
  DateTime now = rtc.now();

  for (int i = 0; i < NUM_PUMPS; i++) {

    // Trigger automatic watering at the scheduled time (once per minute)
    if (myWatering[i].hour   == now.hour()   &&
        myWatering[i].minute == now.minute()  &&
        now.second() < 1                       &&
        !isWatering[i] && !isAutoEnabledPump[i]) {
      enable_pump(i);
      isAutoEnabledPump[i] = true;
      wateringStartTime[i] = millis();
    }

    // Stop automatically after the programmed duration (not in manual mode)
    if (isWatering[i] && !isManual &&
        is_elapsed_time(wateringStartTime[i],
                        (unsigned long)myWatering[i].duration * 60UL * 1000UL)) {
      disable_pump(i);
    }
  }

  // Blink the watering LED as long as at least one pump is running
  if (any_pump_watering()) {
    if (is_elapsed_time(lastLedBlink, ledBlinkDelay)) {
      lastLedBlink = millis();
      ledBlink = !ledBlink;
      digitalWrite(ledWateringPin, ledBlink ? HIGH : LOW);
    }
  }
}


// ═══════════════════════════════════════════════════════════════════════════
//  STANDBY / MANUAL MODE
// ═══════════════════════════════════════════════════════════════════════════

// Turn off the backlight after standbyDelay of inactivity (no button pressed)
int check_standby(int savedWindow) {
  if (digitalRead(buttonNextPin) == HIGH && digitalRead(buttonSelectPin) == HIGH) {
    if (!isStandbyTimer) {
      checkStandbyStartTime = millis();
      isStandbyTimer = true;
    }
  } else {
    isStandbyTimer = false;
    if (!isStandby) lastWindowStandby = savedWindow;
  }

  if (is_elapsed_time(checkStandbyStartTime, standbyDelay)) {
    if (!isStandby) { lcd.clear(); lcd.noBacklight(); }
    isStandby = true;
    return 6;
  }
  if (isStandby) lcd.backlight();
  isStandby = false;
  return lastWindowStandby;
}

// Enter manual mode by holding buttonNext alone for manualDelay ms on Window 0
int check_enable_manual(int savedWindow) {
  if (savedWindow == 0 && digitalRead(buttonNextPin) == LOW) {
    if (is_elapsed_time(manualStartTime, manualDelay)) {
      isManual         = true;
      lastWindowManual = savedWindow;
      manualPump       = 0;
      menuIndex        = 0;
      lcd.clear();
      return 5;
    }
  } else {
    // Reset the hold timer whenever the condition is not met
    manualStartTime = millis();
  }
  return savedWindow;
}


// ═══════════════════════════════════════════════════════════════════════════
//  WINDOWS
// ═══════════════════════════════════════════════════════════════════════════

// ── Window 0 : Overview ────────────────────────────────────────────────────
//
//  Arrosage auto  12h16    <- title + current time (right-aligned)
//  P1 07h30 10m [--]       <- pump 1 status
//  P2 12h00 05m [ON]       <- pump 2 status
//  P3 00h00 10m [--]       <- pump 3 status
//
//  [SEL]          → Window 1 (pump selector)
//  [NEXT] held 5s → Window 5 (manual mode)
//
int Window0() {
  char buf[21];
  DateTime now = rtc.now();

  // Title line: "Arrosage auto  12h16"
  sprintf(buf, "Arrosage auto  %02dh%02d", now.hour(), now.minute());
  lcd.setCursor(0, 0);
  lcd.print(buf);

  for (int i = 0; i < NUM_PUMPS; i++) {
    sprintf(buf, "P%d %02dh%02d %02dm [%s]  ",
            i + 1,
            myWatering[i].hour,
            myWatering[i].minute,
            myWatering[i].duration,
            isWatering[i] ? "ON" : "--");
    lcd.setCursor(0, i + 1);
    lcd.print(buf);
  }

  if (is_button_pressed(buttonSelectPin)) return 1;
  return 0;
}

// ── Window 1 : Pump selector ───────────────────────────────────────────────
//
//  Modification Pompe 1
//  > Modifier
//    Pompe suivante
//    Retour
//
//  [NEXT]         → cycle cursor
//  [SEL] Modifier → Window 2 (config menu for selectedPump)
//  [SEL] Suivante → cycle selectedPump, stay on Window 1
//  [SEL] Retour   → Window 0
//
int Window1() {
  char titleBuf[21];
  sprintf(titleBuf, "Modification Pompe %d", selectedPump + 1);
  lcd.setCursor(0, 0);
  lcd.print(titleBuf);

  create_menu_selector_3(1, "Modifier", "Pompe suivante", "Retour");

  if (is_button_pressed(buttonSelectPin)) {
    switch (menuIndex) {
      case 0:
        tempWatering = myWatering[selectedPump];
        return 2;
      case 1:
        selectedPump = (selectedPump + 1) % NUM_PUMPS;
        menuIndex    = 0;
        lcd.clear();
        break;
      case 2:
        selectedPump = 0;  // reset so next entry starts at pump 1
        return 0;
    }
  }
  return 1;
}

// ── Window 2 : Pump config menu ────────────────────────────────────────────
//
//  Config P1: 07h30 10m
//  > Modifier l'heure
//    Modifier la duree
//    Retour
//
int Window2() {
  char buf[21];
  sprintf(buf, "Config P%d: %02dh%02d %02dm",
          selectedPump + 1,
          myWatering[selectedPump].hour,
          myWatering[selectedPump].minute,
          myWatering[selectedPump].duration);
  lcd.setCursor(0, 0);
  lcd.print(buf);

  create_menu_selector_3(1, "Modifier l'heure",
                             "Modifier la duree",
                             "Retour");

  if (is_button_pressed(buttonSelectPin)) {
    switch (menuIndex) {
      case 0: tempWatering = myWatering[selectedPump]; return 3;
      case 1: tempWatering = myWatering[selectedPump]; return 4;
      case 2: return 1;  // back to pump selector
    }
  }
  return 2;
}

// ── Window 3 : Watering time (hour/minute) ─────────────────────────────────
//
//  Heure pompe 1 :
//         07h30
//  > Modifier
//    Retour
//
//  Editing: [NEXT] increments, [SEL] confirms each field (hours then minutes)
//
int Window3() {
  const int hourCol   = 7;
  const int minuteCol = 10;  // hourCol + 2 digits + 'h'
  const int timeRow   = 1;
  char hourBuf[3], minuteBuf[3];
  sprintf(hourBuf,   "%02d", tempWatering.hour);
  sprintf(minuteBuf, "%02d", tempWatering.minute);

  char titleBuf[21];
  sprintf(titleBuf, "Heure pompe %d :     ", selectedPump + 1);
  lcd.setCursor(0, 0);
  lcd.print(titleBuf);

  if (!isNumberChanging) {
    lcd.setCursor(hourCol, timeRow);
    lcd.print(hourBuf);
    lcd.print("h");
    lcd.print(minuteBuf);
    create_menu_selector(2, "Modifier", "Retour");
  }

  if (isNumberChanging || is_button_pressed(buttonSelectPin)) {
    switch (menuIndex) {
      case 0:
        isNumberChanging = true;

        if (isFirstNumberChanging) {
          // Phase 1: edit hours
          change_displayed_number(hourBuf, hourCol, timeRow, &tempWatering.hour, 24);
          if (is_button_pressed(buttonSelectPin)) {
            lcd.setCursor(hourCol, timeRow);
            lcd.print(hourBuf);
            isFirstNumberChanging = false;
          }
        } else {
          // Phase 2: edit minutes
          change_displayed_number(minuteBuf, minuteCol, timeRow, &tempWatering.minute, 60);
          if (is_button_pressed(buttonSelectPin)) {
            myWatering[selectedPump] = tempWatering;
            EEPROM.put(selectedPump * (int)sizeof(WateringTime), myWatering[selectedPump]);
            isFirstNumberChanging = true;
            isNumberChanging      = false;
          }
        }
        break;
      case 1:
        return 2;
    }
  }
  return 3;
}

// ── Window 4 : Watering duration ───────────────────────────────────────────
//
//  Duree pompe 1 :
//          10m
//  > Modifier
//    Retour
//
int Window4() {
  const int durCol = 8;
  const int durRow = 1;
  char durBuf[3];
  sprintf(durBuf, "%02d", tempWatering.duration);

  char titleBuf[21];
  sprintf(titleBuf, "Duree pompe %d :     ", selectedPump + 1);
  lcd.setCursor(0, 0);
  lcd.print(titleBuf);

  if (!isNumberChanging) {
    lcd.setCursor(durCol, durRow);
    lcd.print(durBuf);
    lcd.print("m");
    create_menu_selector(2, "Modifier", "Retour");
  }

  if (isNumberChanging || is_button_pressed(buttonSelectPin)) {
    switch (menuIndex) {
      case 0:
        isNumberChanging = true;
        change_displayed_number(durBuf, durCol, durRow, &tempWatering.duration, 60);
        if (is_button_pressed(buttonSelectPin)) {
          myWatering[selectedPump] = tempWatering;
          EEPROM.put(selectedPump * (int)sizeof(WateringTime), myWatering[selectedPump]);
          isNumberChanging = false;
        }
        break;
      case 1:
        return 2;
    }
  }
  return 4;
}

// ── Window 5 : Manual mode ─────────────────────────────────────────────────
//
//  Manuel Pompe 1 [--]
//  > Activer
//    Pompe suivante
//    Retour
//
//  Entered by holding buttonNext for 5 s on Window 0.
//  Each pump can be toggled independently; cycling does not change pump state.
//
int Window5() {
  char titleBuf[21];
  sprintf(titleBuf, "Manuel Pompe %d [%s] ",
          manualPump + 1,
          isWatering[manualPump] ? "ON" : "--");
  lcd.setCursor(0, 0);
  lcd.print(titleBuf);

  String opt1 = isWatering[manualPump] ? "Desactiver" : "Activer";
  create_menu_selector_3(1, opt1, "Pompe suivante", "Retour");

  if (is_button_pressed(buttonSelectPin)) {
    switch (menuIndex) {
      case 0:
        if (isWatering[manualPump]) disable_pump(manualPump);
        else                         enable_pump(manualPump);
        break;
      case 1:
        manualPump = (manualPump + 1) % NUM_PUMPS;
        menuIndex  = 0;
        lcd.clear();
        break;
      case 2:
        isManual   = false;
        manualPump = 0;
        return lastWindowManual;
    }
  }
  return 5;
}

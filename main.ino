#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>
#include <RTClib.h>

//I2C
LiquidCrystal_I2C lcd(0x27, 20, 4);
RTC_DS3231 rtc;

//IO
const int ledOnPin = 2;
const int ledWateringPin = 3;
const int relayPumpPin = 4;
const int buttonNextPin = 8;
const int buttonSelectPin = 9;

//General
String selector1 = "> ";
String selector2 = "";
int window = 0;
int menuIndex = 0;
int myWateringEEPROMAddr = 0;
int manualDelay = 5000;
int lastWindow3 = 0;
int lastWindow4 = 0;
int delayButton = 300;
unsigned long lastTimeButtonPressed = 0;
unsigned long manualStartTime = 0;
unsigned long lastNumberBlink = 0;
unsigned long numberBlinkDelay = 500;
unsigned long lastLedBlink = 0;
unsigned long ledBlinkDelay = 500;
unsigned long wateringStartTime = 0;
unsigned long checkStandbyStartTime = 0;
unsigned long standbyDelay = 60000 * 5;
bool isManual = false;
bool numberBlink = false;
bool ledBlink = false;
bool isNumberChanging = false;
bool isFirstNumberChanging = true;
bool isWatering = false;
bool isAutoEnabledPump = false;
bool isStandbyTimer = false;
bool isStandby = false;

struct WateringTime {
  int hour = 0;
  int minute = 0;
  int duration = 10;
} myWatering, tempWatering;


void setup() {
  pinMode(ledOnPin, OUTPUT);
  pinMode(ledWateringPin, OUTPUT);
  pinMode(relayPumpPin, OUTPUT);
  pinMode(buttonNextPin, INPUT_PULLUP);
  pinMode(buttonSelectPin, INPUT_PULLUP);


  EEPROM.get(myWateringEEPROMAddr, myWatering);
  tempWatering = myWatering;

  rtc.begin();
  //rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

  lcd.init();
  lcd.backlight();
}

void loop() {
  digitalWrite(ledOnPin, HIGH);

  check_plants_watering();
  window = check_enable_manual(window);
  window = check_standby(window);

  switch (window) {
    case 0:
      window = Window0();
      refresh_window_if_changed(0);
      break;
    case 1:
      window = Window1();
      refresh_window_if_changed(1);
      break;
    case 2:
      window = Window2();
      refresh_window_if_changed(2);
      break;
    case 3:
      window = Window3();
      refresh_window_if_changed(3);
      break;
    case 4:
      break;
  }
}

int check_standby(int savedWindow) {
  if (digitalRead(buttonNextPin) == HIGH && digitalRead(buttonSelectPin) == HIGH) {
    if (!isStandbyTimer) {
      checkStandbyStartTime = millis();
      isStandbyTimer = true;
    }
  } else {
    isStandbyTimer = false;
    if (!isStandby) {
      lastWindow4 = savedWindow;
    }
  }

  if (is_elapsed_time(checkStandbyStartTime, standbyDelay)){
    if (!isStandby) {
      lcd.clear();
      lcd.noBacklight();
    }
    isStandby = true;

    return 4;
  } else {
    if (isStandby) {
      lcd.backlight();
    }
    isStandby = false;

    return lastWindow4;
  }
}

int check_enable_manual(int savedWindow){
  if (digitalRead(buttonNextPin) == LOW && digitalRead(buttonSelectPin) == LOW && savedWindow != 3) {
    if (is_elapsed_time(manualStartTime, manualDelay)){
      isManual = true;
      lastWindow3 = savedWindow;
      lcd.clear();

      return 3;
    }
  } else {
    manualStartTime = millis();
  }

  return savedWindow;
}

void enable_pump() {
  isWatering = true;
  digitalWrite(relayPumpPin, HIGH);
}

void disable_pump() {
  isWatering = false;
  isAutoEnabledPump = false;
  digitalWrite(relayPumpPin, LOW);
  digitalWrite(ledWateringPin, LOW);
}

void check_plants_watering() {
  DateTime now = rtc.now();

  if (myWatering.hour == now.hour() && myWatering.minute == now.minute() && now.second() < 1 && !isWatering && !isAutoEnabledPump) {
    enable_pump();
    isAutoEnabledPump = true;
    wateringStartTime = millis();
  }

  if (isWatering) {
    if (is_elapsed_time(lastLedBlink, ledBlinkDelay)) {
      lastLedBlink = millis();
      ledBlink = !ledBlink;

      if (ledBlink) {
        digitalWrite(ledWateringPin, HIGH);
      } else {
        digitalWrite(ledWateringPin, LOW);
      }
    }

    if (is_elapsed_time(wateringStartTime, (unsigned long)(myWatering.duration) * 60 * 1000) && !isManual) {
      disable_pump();
    }
  }

  return 0;
}

bool is_elapsed_time(unsigned long startTime, unsigned long durationTime) {
  return millis() - startTime >= durationTime;
}

void refresh_window_if_changed(int currentWindow) {
  if (window != currentWindow) {
    lcd.clear();
    menuIndex = 0;
    selector1 = "> ";
    selector2 = "";
  }
}

void lcd_print_padding(int number){
  for (int i = 0; i < number; i++) {
    lcd.print(" ");
  }
}

void change_displayed_number(char number[3], int column, int row, int *numberPtr, int maxNumber){
    blink_number(number, column, row);

    if (is_button_pressed(buttonNextPin)) {
      (*numberPtr)++;
      if (*numberPtr == maxNumber) {
        *numberPtr = 0;
      }
      sprintf(number, "%02d", *numberPtr);
      lcd.setCursor(column,row);
      lcd.print(number);
    }
}

void blink_number(char number[3], int column, int row) {
  if (is_elapsed_time(lastNumberBlink, numberBlinkDelay)) {
    lastNumberBlink = millis();
    numberBlink = !numberBlink;

    lcd.setCursor(column,row);
    if (numberBlink) {
      lcd.print(number);
    } else {
      lcd.print("  ");
    }
  }
}

bool is_button_pressed(int buttonPin) {
  if (digitalRead(buttonPin) == LOW && is_elapsed_time(lastTimeButtonPressed, delayButton)) {
    lastTimeButtonPressed = millis();

    return true;
  }

  return false;
}

void create_menu_selector(int startRow, String option1, String option2) {
  int secondRow = startRow + 1;
  int option1Padding = 20 - (option1.length() + selector1.length());
  int option2Padding = 20 - (option2.length() + selector2.length());

  lcd.setCursor(0,startRow);
  lcd.print(selector1);
  lcd.print(option1);
  lcd_print_padding(option1Padding);
  lcd.setCursor(0, secondRow);
  lcd.print(selector2);
  lcd.print(option2);
  lcd_print_padding(option2Padding);

  if (is_button_pressed(buttonNextPin)) {
    switch (menuIndex) {
      case 0:
        menuIndex = 1;
        selector1 = "";
        selector2 = "> ";
        break;
      case 1:
        menuIndex = 0;
        selector1 = "> ";
        selector2 = "";
        break;
    }
  }
}

int Window0() {
  char timeBuffer[6];
  char durationBuffer[3];
  sprintf(timeBuffer, "%02dh%02d", myWatering.hour, myWatering.minute);
  sprintf(durationBuffer, "%02dm", myWatering.duration);

  lcd.setCursor(0, 0);
  lcd.print("Arrosage automatique");
  lcd.setCursor(0, 1);
  lcd.print("  [ ");
  lcd.print(timeBuffer);
  lcd.print(" - ");
  lcd.print(durationBuffer);
  lcd.print(" ]");

  create_menu_selector(2, "Modifier l'heure", "modifier le temps");

  if (is_button_pressed(buttonSelectPin)) {
    switch (menuIndex) {
      case 0:
        return 1;
        break;
      case 1:
        return 2;
        break;
    }
  }

  return 0;
}

int Window1() {
  int hourColumn = 7;
  int minuteColumn = hourColumn + 3;
  int timeRow = 1;
  char hourBuffer[3];
  char minuteBuffer[3];
  sprintf(hourBuffer, "%02d", tempWatering.hour);
  sprintf(minuteBuffer, "%02d", tempWatering.minute);

  lcd.setCursor(0,0);
  lcd.print("Heure d'arrosage :");
  if (isNumberChanging == false) {
    lcd.setCursor(hourColumn,timeRow);
    lcd.print(hourBuffer);
    lcd.print("h");
    lcd.print(minuteBuffer);
  
    create_menu_selector(2, "Modifier", "Retour");
  }

  if (isNumberChanging == true || is_button_pressed(buttonSelectPin)) {
    switch (menuIndex) {
      case 0:
        isNumberChanging = true;

        if (isFirstNumberChanging == true) {
          change_displayed_number(hourBuffer, hourColumn, timeRow, &tempWatering.hour, 24);

          if (is_button_pressed(buttonSelectPin)) {
            lcd.setCursor(hourColumn,timeRow);
            lcd.print(hourBuffer);
            isFirstNumberChanging = false;
          }
        }

        if (isFirstNumberChanging == false) {
          change_displayed_number(minuteBuffer, minuteColumn, timeRow, &tempWatering.minute, 60);

          if (is_button_pressed(buttonSelectPin)) {
            myWatering = tempWatering;
            EEPROM.put(myWateringEEPROMAddr, myWatering);
            isFirstNumberChanging = true;
            isNumberChanging = false;
          }
        }
        break;
      case 1:
        return 0;
        break;
    }
  }

  return 1;
}

int Window2() {
  int durationColumn = 8;
  int durationRow = 1;
  char durationBuffer[3];
  sprintf(durationBuffer, "%02d", tempWatering.duration);

  lcd.setCursor(0,0);
  lcd.print("Temps d'arrosage :");
  if (isNumberChanging == false) {
    lcd.setCursor(durationColumn,durationRow);
    lcd.print(durationBuffer);
    lcd.print("m");

    create_menu_selector(2, "Modifier", "Retour");
  }

  if (isNumberChanging == true || is_button_pressed(buttonSelectPin)) {
    switch (menuIndex) {
      case 0:
        isNumberChanging = true;
        change_displayed_number(durationBuffer, durationColumn, durationRow, &tempWatering.duration, 60);

        if (is_button_pressed(buttonSelectPin)) {
          myWatering = tempWatering;
          EEPROM.put(myWateringEEPROMAddr, myWatering);
          isNumberChanging = false;
        }
        break;
      case 1:
        return 0;
        break;
    }
  }

  return 2;
}

int Window3() {
  String option1 = "";

  lcd.setCursor(0,0);
  lcd.print("Mode manuel :");
  lcd.setCursor(0,1);
  if (isWatering) {
    lcd.print("     [ Activee ]    ");
    option1 = "Desactiver";
  } else {
    lcd.print("   [ Desactivee ]   ");
    option1 = "Activer";
  }

  create_menu_selector(2, option1, "Retour");

  if (is_button_pressed(buttonSelectPin)) {
    switch (menuIndex) {
      case 0:
        if (!isWatering) {
          enable_pump();
        } else {
          disable_pump();
        }
        break;
      case 1:
        isManual = false;

        return lastWindow3;
        break;
    }
  }

  return 3;
}

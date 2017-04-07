#include <Arduino.h>

#include <EEPROM.h>
#include <Wire.h>
#include <LiquidTWI2.h>

// lcd backlight color
#define OFF 0x0
#define RED 0x1
#define YELLOW 0x3
#define GREEN 0x2
#define TEAL 0x6
#define BLUE 0x4
#define VIOLET 0x5
#define WHITE 0x7

LiquidTWI2 lcd(0x20);

const uint8_t EEPROM_START = 32;
const uint16_t EEPROM_VERSION = 0xcdd4; // This should change if NUM_CYCLES or struct Sched changes. Random.

const int VALVE_PIN = 12;
const int NUM_MENU_ITEMS = 7;
const int NUM_CYCLES = 4;
const int MAX_CYCLE_LENGTH = 240; // minutes
const int DEFAULT_MANUAL_DURATION = 30; // default for manual_duration
const int DELAY_SCROLL = 200;
const int DELAY_SPLASH = 2000;
const int DAY_IN_MINUTES = 1440;
const unsigned long DAY_IN_MS = 86400000UL;
const unsigned long HOUR_IN_MS = 3600000UL;
const unsigned long MINUTE_IN_MS = 60000UL;
const unsigned long SECOND_IN_MS = 1000UL;

unsigned long midnight = 0; // midnight, in the past, for comparison to time
unsigned long time = 0; // time as returned by millis()
unsigned long time_of_day = 0; // Seconds since midnight

int manual_duration = DEFAULT_MANUAL_DURATION; // duration of manual cycle in minutes

int set_cycle = 0; // current cycle being set in s_set_sched... states

struct Sched {
  unsigned int start; // start times in minutes after midnight
  unsigned int length; // length of each cycle in minutes
  boolean enabled;
}
mySched[NUM_CYCLES];

// unsigned int start_time[NUM_CYCLES];  // start times in minutes after midnight
// unsigned int cycle_length[NUM_CYCLES]; // length of each cycle in minutes
// boolean cycle_enabled[NUM_CYCLES];

unsigned long last_button_time = 0;
unsigned long backlight_timeout = 10000;
boolean backlight_on = true;
boolean time_is_set = false;

uint8_t buttons = 0; // button state

void (*state)() = NULL;

void (*previous_state)() = NULL;

unsigned long state_change_time = 0;

// updates time midnight and time_of_day
void updateTime() {
  time =  millis();
  while (time - midnight >= DAY_IN_MS)
    midnight += DAY_IN_MS;
  time_of_day = (time - midnight) / SECOND_IN_MS;
}

// Prints time of day to lcd.
void printTime() {
  unsigned long seconds = time_of_day;
  unsigned int minutes = 0;
  unsigned int hours = 0;
  while (seconds >= 60) {
    seconds -= 60;
    minutes++;
  }
  while (minutes >= 60) {
    minutes -= 60;
    hours++;
  }
  lcd.setCursor(8, 0);
  char time_str[9];
  sprintf(time_str, "%02d:%02d:%02d", hours, minutes, seconds);
  lcd.print(time_str);
}

// like constrain but wraps. Unlike constrain, upper is non inclusive. Lower bound is always zero.
int constrain_wrap(int i, int upper) {
  while (i >= upper)
    i -= upper;
  while (i < 0)
    i += upper;
  return i;
}

// print time being set HH:MM
void printSetTime(int minutes) {
  if (minutes < 0) // minutes should be positive
    minutes = 0;
  int hours = 0;
  while (minutes >= 60) {
    minutes -= 60;
    hours++;
  }
  hours = constrain(hours, 0, 99); // our string can only hold 2 hours digits
  char time_str[6];
  sprintf(time_str, "%02d:%02d", hours, minutes);
  lcd.setCursor(11, 1);
  lcd.print(time_str);
}

void printMenuText(int selection) {
  lcd.setCursor(0, 1);
  if (selection == 0)
    lcd.print(F("Manual          "));
  else if (selection == 1)
    lcd.print(F("Set Time        "));
  else if (selection == 2)
    lcd.print(F("Set Schedule    "));
  else if (selection == 3)
    lcd.print(F("Auto            "));
  else if (selection == 4)
    lcd.print(F("Info            "));
  else if (selection == 5)
    lcd.print(F("Load Sched      "));
  else if (selection == 6)
    lcd.print(F("Save Sched      "));
}

void menuSelect(int selection) {
  if (selection == 0)
    state = s_manual_begin;
  if (selection == 1)
    state = s_set_time_begin;
  if (selection == 2)
    state = s_set_sched_begin;
  if (selection == 3)
    state = s_auto_begin;
  if (selection == 4)
    state = s_info_begin;
  if (selection == 5)
    state = s_eeprom_load;
  if (selection == 6)
    state = s_eeprom_save;
}

void backlightState() {
  if (!buttons && (time - last_button_time > backlight_timeout)) {
    if (time_is_set) {
      if (state == s_auto)
        lcd.setBacklight(GREEN);
      else
        lcd.setBacklight(OFF);
    }
    else
      lcd.setBacklight(RED);
    backlight_on = false;
  }
  else if (buttons && backlight_on)
    last_button_time = time;
  else if (buttons && !backlight_on) {
    last_button_time = time;
    buttons = 0;
    lcd.setBacklight(WHITE);
    backlight_on = true;
  }
}

void setup() {
  // set midnight based on compile time
  char time_str[] = __TIME__;
  char buf[3];
  buf[2] = byte(0);
  buf[0] = time_str[0];
  buf[1] = time_str[1];
  int hour = atoi(buf);
  buf[0] = time_str[3];
  buf[1] = time_str[4];
  int minute = atoi(buf);
  buf[0] = time_str[6];
  buf[1] = time_str[7];
  int second = atoi(buf);
  midnight = 0UL - ((unsigned long)hour * HOUR_IN_MS + (unsigned long)minute * MINUTE_IN_MS + (unsigned long)second * SECOND_IN_MS);

  // initialise auto vars
  for (int i = 0; i < NUM_CYCLES; i++) {
    mySched[i].start = 0;
    mySched[i].length = 30;
    mySched[i].enabled = false;
  }

  lcd.setMCPType(LTI_TYPE_MCP23017);
  lcd.begin(16, 2);
  lcd.setBacklight(WHITE);
  pinMode(VALVE_PIN, OUTPUT);

  state = s_splash_begin;
}

void loop() {
  updateTime();

  if (state != previous_state) {
    state_change_time = time;
    previous_state = state;
  }

  buttons = lcd.readButtons();

  backlightState();

  state();

  while (millis() - time < DELAY_SCROLL)
    delay(1);
}

void s_splash_begin() {
  lcd.clear();
  lcd.print(F("Sprinkler Timer"));
  state = s_splash;
}

void s_splash() {
  if (time - state_change_time > DELAY_SPLASH)
    state = s_menu_begin;
}

void s_menu_begin() {
  lcd.clear();
  lcd.print(F("Menu"));
  state = s_menu;
}

void s_menu() {
  static int selection = 0;
  printTime();
  if (buttons & BUTTON_UP)
    selection++;
  if (buttons & BUTTON_DOWN)
    selection--;
  selection = constrain_wrap(selection, NUM_MENU_ITEMS);
  printMenuText(selection);
  if (buttons & BUTTON_RIGHT)
    menuSelect(selection);
}

void s_auto_begin() {
  lcd.clear();
  lcd.print(F("Auto"));
  state = s_auto;
}

void s_auto() {
  boolean valve_on = false;
  printTime();
  lcd.setCursor(0, 1);
  for (int i = 0; i < NUM_CYCLES; i++) {
    if (mySched[i].enabled && (time_of_day > ((unsigned long)mySched[i].start * 60UL)) && (time_of_day < (unsigned long)(mySched[i].start + mySched[i].length) * 60UL)) {
      valve_on = true;
      lcd.print(i);
      lcd.print(F(":+ "));
    }
    else {
      lcd.print(i);
      lcd.print(F(":- "));
    }
  }
  if (valve_on)
    digitalWrite(VALVE_PIN, HIGH);
  else
    digitalWrite(VALVE_PIN, LOW);
  if (buttons)
    state = s_auto_end;
}

void s_auto_end() {
  digitalWrite(VALVE_PIN, LOW);
  state = s_menu_begin;
}

void s_manual_begin() {
  lcd.clear();
  lcd.print(F("Manual"));
  lcd.setCursor(0, 1);
  lcd.print(F("Duration"));
  manual_duration = DEFAULT_MANUAL_DURATION;
  state = s_manual_set;
}

void s_manual_set() {
  printTime();
  if (buttons & BUTTON_RIGHT)
    state = s_manual_wait_begin;
  else if (buttons & BUTTON_UP)
    manual_duration++;
  else if (buttons & BUTTON_DOWN)
    manual_duration--;
  manual_duration = constrain_wrap(manual_duration, MAX_CYCLE_LENGTH + 1);
  printSetTime(manual_duration);
}

void s_manual_wait_begin() {
  lcd.setCursor(0, 1);
  lcd.print(F("On              "));
  digitalWrite(VALVE_PIN, HIGH);
  state = s_manual_wait;
}

void s_manual_wait() {
  printTime();
  unsigned long time_remaining = ((unsigned long)manual_duration * MINUTE_IN_MS) - (time - state_change_time);
  if (time_remaining >= HOUR_IN_MS) { // if time_remaining is an hour or more, convert to minutes
    time_remaining = time_remaining / MINUTE_IN_MS; // convert from ms to minutes
  }
  else { // less than an hour, convert to seconds
    time_remaining = time_remaining / SECOND_IN_MS;// convert from ms to seconds
  }
  printSetTime((int)time_remaining);
  if ((time - state_change_time) > ((unsigned long)manual_duration * MINUTE_IN_MS))
    state = s_manual_end;
  if (buttons)
    state = s_manual_end;
}

void s_manual_end() {
  digitalWrite(VALVE_PIN, LOW);
  state = s_menu_begin;
}

void s_set_time_begin() {
  lcd.clear();
  lcd.print(F("Set Time"));
  lcd.setCursor(0, 1);
  lcd.print(F("Hours           "));
  state = s_set_time_hours;
}

void s_set_time_hours() {
  printTime();
  if (buttons & BUTTON_DOWN) {
    midnight += HOUR_IN_MS;
  }
  else if (buttons & BUTTON_UP) {
    midnight -= HOUR_IN_MS;
  }
  else if (buttons & BUTTON_RIGHT) {
    lcd.setCursor(0, 1);
    lcd.print(F("Minutes         "));
    state = s_set_time_minutes;
  }
}

void s_set_time_minutes() {
  printTime();
  if (buttons & BUTTON_DOWN) {
    midnight += MINUTE_IN_MS;
  }
  else if (buttons & BUTTON_UP) {
    midnight -= MINUTE_IN_MS;
  }
  else if (buttons & BUTTON_RIGHT) {
    lcd.setCursor(0, 1);
    lcd.print(F("Seconds         "));
    state = s_set_time_seconds;
  }
}

void s_set_time_seconds() {
  printTime();
  if (buttons & BUTTON_DOWN) {
    midnight += SECOND_IN_MS;
  }
  else if (buttons & BUTTON_UP) {
    midnight -= SECOND_IN_MS;
  }
  else if (buttons & BUTTON_RIGHT)
    state = s_set_time_end;
}

void s_set_time_end() {
  time_is_set = true;
  state = s_menu_begin;
}

void s_set_sched_begin() {
  lcd.clear();
  lcd.print(F("Sched "));
  lcd.print(set_cycle + 1);
  lcd.print(F(" Start"));
  lcd.setCursor(0, 1);
  lcd.print(F("Hour       "));
  state = s_set_sched_hour;
}

void s_set_sched_hour() {
  if (buttons & BUTTON_RIGHT) {
    lcd.setCursor(0, 1);
    lcd.print(F("Minute     "));
    state = s_set_sched_minute;
  }
  else if (buttons & BUTTON_UP) {
    mySched[set_cycle].start += 60;
  }
  else if (buttons & BUTTON_DOWN) {
    mySched[set_cycle].start -= 60;
  }
  mySched[set_cycle].start = constrain_wrap(mySched[set_cycle].start, DAY_IN_MINUTES);
  printSetTime(mySched[set_cycle].start);
}

void s_set_sched_minute() {
  if (buttons & BUTTON_RIGHT) {
    lcd.setCursor(8, 0);
    lcd.print(F("        "));
    lcd.setCursor(0, 1);
    lcd.print(F("Duration   "));
    state = s_set_sched_duration;
  }
  else if (buttons & BUTTON_UP) {
    mySched[set_cycle].start++;
  }
  else if (buttons & BUTTON_DOWN) {
    mySched[set_cycle].start--;
  }
  mySched[set_cycle].start = constrain_wrap(mySched[set_cycle].start, DAY_IN_MINUTES);
  printSetTime(mySched[set_cycle].start);
}

void s_set_sched_duration() {
  if (buttons & BUTTON_RIGHT) {
    lcd.setCursor(0, 1);
    lcd.print(F("Enable     "));
    state = s_set_sched_enable;
  }
  else if (buttons & BUTTON_UP) {
    mySched[set_cycle].length++;
  }
  else if (buttons & BUTTON_DOWN) {
    mySched[set_cycle].length--;
  }
  mySched[set_cycle].length = constrain_wrap(mySched[set_cycle].length, MAX_CYCLE_LENGTH + 1);
  printSetTime(mySched[set_cycle].length);
}

void s_set_sched_enable() {
  if (buttons & BUTTON_RIGHT) {
    state = s_set_sched_end;
  }
  else if ((buttons & BUTTON_UP) || (buttons & BUTTON_DOWN))
    mySched[set_cycle].enabled = !mySched[set_cycle].enabled;
  lcd.setCursor(11, 1);
  if (mySched[set_cycle].enabled)
    lcd.print(F("On   "));
  else
    lcd.print(F("Off  "));
}

void s_set_sched_end () {
  if (set_cycle >= NUM_CYCLES - 1) {
    set_cycle = 0;
    state = s_menu_begin;
  }
  else {
    set_cycle++;
    state = s_set_sched_begin;
  }
}

void s_info_begin() {
  lcd.clear();
  lcd.print(F("Compiled"));
  state = s_info;
}

void s_info() {
  if (time - state_change_time > DELAY_SPLASH * 4)
    state = s_menu_begin;
  else if (time - state_change_time > DELAY_SPLASH * 3) {
    lcd.home();
    lcd.print(F("djones.co       "));
    lcd.setCursor(0, 1);
    lcd.print(F("/sprinkler      "));
  }
  else if (time - state_change_time > DELAY_SPLASH * 2) {
    lcd.home();
    lcd.print(F("EEPROM version  "));
    lcd.setCursor(0, 1);
    lcd.print(EEPROM_VERSION, 16);
    lcd.print(F("            "));
  }
  else if (time - state_change_time > DELAY_SPLASH) {
    lcd.setCursor(0, 1);
    lcd.print(F(__TIME__));
    lcd.print(F("        "));
  }
  else {
    lcd.setCursor(0, 1);
    lcd.print(F(__DATE__));
  }
}

void s_eeprom_save() {
  lcd.clear();
  lcd.print(F("Saving to EEPROM"));

  int bytes_saved = 0;

  int bytes_size = sizeof(mySched);
  for (int i = 0; i < bytes_size; i++) {
    EEPROM.write(EEPROM_START + i, *((char*)&mySched + i));
  }
  bytes_saved += bytes_size;

  bytes_size = sizeof(EEPROM_VERSION);
  for (int i = 0; i < bytes_size; i++) {
    EEPROM.write(EEPROM_START + bytes_saved + i, *((char*)&EEPROM_VERSION + i));
  }
  bytes_saved += bytes_size;

  lcd.setCursor(0, 1);
  lcd.print(bytes_saved);
  lcd.print(F(" bytes saved"));
  state = s_eeprom_wait;
}

void s_eeprom_load() {
  lcd.clear();
  lcd.print(F("Loading EEPROM"));
  lcd.setCursor(0, 1);

  Sched loadSched[NUM_CYCLES];
  uint16_t loadVersion = 0;

  int bytes_loaded = 0;

  int bytes_size = sizeof(loadSched);
  for (int i = 0; i < bytes_size; i++) {
    *((char*)&loadSched + i) = EEPROM.read(EEPROM_START + i);
  }
  bytes_loaded += bytes_size;

  bytes_size = sizeof(loadVersion);
  for (int i = 0; i < bytes_size; i++) {
    *((char*)&loadVersion + i) = EEPROM.read(EEPROM_START + bytes_loaded + i);
  }
  bytes_loaded += bytes_size;

  if (loadVersion == EEPROM_VERSION) {
    for (int i = 0; i < NUM_CYCLES; i++) {
      mySched[i] = loadSched[i];
    }
    lcd.print(bytes_loaded);
    lcd.print(F(" bytes loaded"));
  }
  else {
    lcd.print(F("Version mismatch"));
  }
  state = s_eeprom_wait;
}

void s_eeprom_wait() {
  if (buttons)
    state = s_menu_begin;
}

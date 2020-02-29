#include <Ardudim.h>
#include <avr/eeprom.h>
#include <avr/sleep.h>
#include "OneButton.h"
#include <Wire.h>
#include "Seeed_QTouch.h"

#define VERSION "Ardudim V_12, 02/2020"

#define VSENS_PERIOD      21    // AC detection time[ms]
#define TOUCH_SHORT       50    // minimum time [ms] for a short touch, VSENS only
#define TOUCH_LONG        300   // time [ms] for a long touch

#define VOLTAGE_SAMPLES   10
#define MAX_VOLTAGE       500   // 50V, over voltage detection

#define FRAME_TIME        10    // ms, 10 = 100 frames/s
#define DIM_FADE_STEP     8     // steps per frame when fading
#define COLOR_FADE_STEP   4     // steps per frame for color fading
#define COLOR_STEPS       4     // number of color steps

#define COLORTEMP_MIN     5     // minimum color temperature (tunable mode only)
#define COLORTEMP_MAX     250   // maximum color temperature

#define SLEEP_TIMEOUT     5000  // ms

#define LED_DURATION      250   // status LED blink speed

/* Menu properties */
#define MENU_ENTRIES      {3, 3, 3, 2, 1}
#define MENU_TIMEOUT      15000 // ms

/* Pins */
#define PIN_TOUCH         2
#define PIN_SDA           18
#define PIN_SCL           19

/* Button states */
#define BTN_CLICK         1
#define BTN_DOUBLE_CLICK  2
#define BTN_DURING_LONG   3
#define BTN_AFTER_LONG    4

/* Touch interface types */
#define TOUCH_NO          0     // no touch interface
#define TOUCH_INT         1     // internal 2 touch buttons
#define TOUCH_EXT         2     // external 4 touch buttons
#define TOUCH_VSENS       3     // external AC sensor touch, one button
#define TOUCH_BUTTONS     5     // push-button interface
#define VSENS_IDLE        0
#define VSENS_ACTIVE      1

/* Light control */
#define DIM_STOP          0
#define DIM_UP            1
#define DIM_DOWN          2
#define COLOR_STOP        0
#define COLOR_WARMER      1
#define COLOR_COOLER      2

ardudim Ardudim;
OneButton button(PIN_BUTTON, true);

uint8_t disable_sleep = 0;
uint8_t actual_touch = TOUCH_NO;

/* Reset */
void(* resetFunct) (void) = 0;

/* Configuration */
struct {
  uint8_t op_mode;            //operating mode
  uint8_t touch;              //touch interface
  uint16_t current[CHANNELS]; //max. output current
  uint8_t brightness;         //power-on brightness
  uint8_t colortemp;          //power-on color temperature
  uint8_t sleep_enable;       //power save mode, 1=disabled, 2=enabled
  uint8_t checksum;
} cfg;

/* A channel */
typedef struct {
  uint8_t value;
  uint8_t actual_level;
  uint8_t dim_direction;
  uint8_t prev_dim_direction;
} channel_t;

/* Voltage */
struct {
  uint16_t startup = 0;
  uint16_t samples[VOLTAGE_SAMPLES];
  uint8_t index = 0;
  uint8_t warning = 0;
} voltage;

/* Simple checksum for EEPROM configuration data */
uint8_t config_checksum(void) {
  uint8_t sum = 0;
  uint8_t *p = (uint8_t *)&cfg;
  for (unsigned int i = 0; i < sizeof(cfg) - 1; i++) {
    sum += p[i];
  }
  return sum;
}

/* Check if the configuration looks valid */
int valid_config(void) {
  if (cfg.checksum != config_checksum()) return 0;
  if (cfg.touch > 4) return 0;
  if (cfg.op_mode > 3) return 0;
  return 1;
}

/* Prints the current configuration */
void print_config(void) {
  Serial.println(F("\nConfiguration\n-------------"));
  Serial.print(F("Mode: "));
  Serial.print(cfg.op_mode);
  Serial.print(" - ");
  switch (cfg.op_mode) {
    case ONE_CHANNEL:
      Serial.println(F("One channel"));
      break;
    case TWO_CHANNEL:
      Serial.println(F("Two channels"));
      break;
    case TUNABLE:
      Serial.println(F("Tunable white"));
      break;
  }
  Serial.print(F("LED channel currents: "));
  for (int i = 0; i < CHANNELS; i++) {
    Serial.print(map(cfg.current[i], 0, 65535, 0, 700));
    Serial.print(F("mA "));
  }
  Serial.println();
  Serial.print(F("Touch mode: "));
  Serial.print(cfg.touch);
  Serial.print(" - ");
  switch (cfg.touch) {
    case TOUCH_NO:
      Serial.println(F("none"));
      break;
    case TOUCH_INT:
      Serial.println(F("I2C internal 2 inputs"));
      break;
    case TOUCH_EXT:
      Serial.println(F("I2C external 4 inputs"));
      break;
    case TOUCH_VSENS:
      Serial.println(F("AC sensor touch"));
      break;
  }
}

/* Set the default config */
void default_config(void) {
  cfg.op_mode = ONE_CHANNEL;
  cfg.touch = TOUCH_INT;
  for (int i = 0; i < CHANNELS; i++) {
    cfg.current[i] = CURRENT_350;
  }
  cfg.brightness = 255;
  cfg.colortemp = 128;
  cfg.sleep_enable = 1; // sleep disabled
  cfg.checksum = config_checksum();
}

/* Write current config to EEPROM */
void write_config(void) {
  cfg.checksum = config_checksum();
  eeprom_write_block((const void*)&cfg, (void*)0, sizeof(cfg));
  Serial.println(F("Configuration written."));
}

/* Commit settings made in the button menu */
void menu_save_setting(uint8_t item, uint8_t setting) {
  switch (item) {
    // Mode
    case 1:
      // ONE_CHANNEL, TWO_CHANNEL, TUNABLE
      cfg.op_mode = setting;
      break;
    // Output current
    case 2:
      uint16_t current;
      if (setting == 1) current = CURRENT_350;
      else if (setting == 2) current = CURRENT_500;
      else if (setting == 3) current = CURRENT_700;
      else current = CURRENT_350;
      for (int i = 0; i < CHANNELS; i++) {
        cfg.current[i] = current;
      }
      Ardudim.setCurrent(current);
      Ardudim.setBrightness(255);
      break;
    case 3:
      cfg.touch = setting;
      init_touch();
      break;
    case 4:
      cfg.sleep_enable = setting;
      break;
    case 5:
      default_config();
      for (int i = 0; i < CHANNELS; i++) {
        Ardudim.setCurrent(i, cfg.current[i]);
      }
      Ardudim.setBrightness(255);
      init_touch();
      break;
  }
  write_config();
  print_config();
}

/* The button menu */
void menu(uint8_t btn) {
  uint8_t entries[] = MENU_ENTRIES;
  static uint8_t state = 0, item = 0, setting = 0, led = 0;
  static unsigned long led_time = 0, menu_timeout = 0;
  unsigned long now;

  if (btn) {
    // button has been pressed
    menu_timeout = millis() + MENU_TIMEOUT;
    switch (state) {
      // menu off
      case 0:
        if (btn == BTN_DURING_LONG) {
          state = 1;
          disable_sleep |= 1 << 2;
        }
        break;
      // signal "long ok"
      case 1:
        if (btn == BTN_AFTER_LONG) {
          // enter menu
          item = 1;
          setting = 0;
          state = 2;
        }
        break;
      // item select
      case 2:
        if (btn == BTN_CLICK) {
          if (item < sizeof(entries)) item ++;
          else item = 1;
          LED_OFF;
          led = 0;
        }
        if (btn == BTN_DURING_LONG) {
          state = 3;
        }
        break;
      // signal "item select ok"
      case 3:
        if (btn == BTN_AFTER_LONG) {
          setting = 1;
          state = 4;
        }
        break;
      // setting select
      case 4:
        if (btn == BTN_CLICK) {
          if (setting < entries[item - 1]) setting ++;
          else setting = 1;
          LED_OFF;
          led = 0;
        }
        if (btn == BTN_DURING_LONG) {
          state = 5;
        }
        break;
      // signal "set ok"
      case 5:
        if (btn == BTN_AFTER_LONG) {
          menu_save_setting(item, setting);
          state = 0;
          disable_sleep &= ~(1 << 2);
          LED_OFF;
        }
        break;
    }
  }
  else {
    if (state == 0) {
      return;
    }
    now = millis();
    if (state == 1 || state == 3 || state == 5) {
      LED_ON;
    }
    else if (state > 1) {
      if (now > led_time) {
        if (led) {
          if (led % 2) {
            LED_OFF;
          }
          else {
            LED_ON;
          }
          led --;
          led_time = now + LED_DURATION;
        }
        else {
          if (setting) {
            led = setting * 2;
          }
          else {
            led = item * 2;
          }
          led_time = now + LED_DURATION * 2;
        }
      }
    }

    if (now > menu_timeout) {
      state = 0;
      disable_sleep &= ~(1 << 2);
      LED_OFF;
    }
  }
}

/* Energy saving sleep */
void wake_up(void) {
}
void go_to_sleep(void) {
  Ardudim.setPower(0);
  Serial.println(F("sleeping"));
  delay(100);
  attachInterrupt(0, wake_up, LOW);
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  sleep_mode();
  sleep_disable();
  detachInterrupt(0);
  Ardudim.setPower(1);
  Serial.println(F("woke up"));
}

/* Callbacks for OneButton library */
void button_click() {
  menu(BTN_CLICK);
}
void button_during_long() {
  menu(BTN_DURING_LONG);
}
void button_after_long() {
  menu(BTN_AFTER_LONG);
}

/* Print a voltage human readable */
void print_voltage (uint16_t voltage) {
  //voltage is in 1/10V
  Serial.print(float(voltage) / 10);
  Serial.println("V");
}

/* Fade with limits used for brightness and color temperature */
uint8_t fade(uint8_t actual_value, uint8_t target_value, uint8_t step_size) {
  int16_t diff = target_value - actual_value;
  if (abs(diff) < step_size) {
    return target_value;
  }
  if (diff > 0) {
    return actual_value + step_size;
  }
  // diff > -step_size
  return actual_value - step_size;
}

/*  Try to find out what is connected to a pin */
int detect_button(uint8_t pin) {
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  delayMicroseconds(100);
  pinMode(pin, INPUT);
  delayMicroseconds(100);
  if (digitalRead(pin)) {
    // likely an I2C pull-up
    return 0;
  }
  delay(10);
  if (digitalRead(pin)) {
    //probably a button
    return 1;
  }
  else {
    //open pin or pushed button
    return 2;
  }
}

/* Setup touch */
int init_touch() {
  actual_touch = cfg.touch;
  if (cfg.touch == TOUCH_VSENS) {
    // only the AC sensor touch input
    pinMode(PIN_TOUCH, INPUT);
    digitalWrite(PIN_TOUCH, LOW);
    Wire.end();
    Serial.println(F("Using AC sensor touch input."));
    return 1;
  }
  Wire.end();
  if (detect_button(PIN_SDA) || detect_button(PIN_SCL)) {
    Serial.println(F("Detected push-button interface."));
    // used just like the internal touch buttons
    actual_touch = TOUCH_BUTTONS;
    return 1;
  }
  Wire.begin();
  if (QTouch.chipPresent()) {
    Serial.println(F("AT42QT1070 Touch Controller found"));
    QTouch.reset();
    QTouch.setMaxDuration(31); //5s
    delay(500);
    if (cfg.touch == TOUCH_INT) {
      QTouch.setGroup(0, 0xff); //Guard, on-board
      QTouch.setGroup(1, 0xff);
      QTouch.setGroup(2, 0xff);
      QTouch.setGroup(3, 0xff);
      QTouch.setGroup(4, 0xff);
      QTouch.setGroup(5, 1); //on-board
      QTouch.setGroup(6, 2); //on-board
    }
    else if (cfg.touch == TOUCH_EXT) {
      QTouch.setGroup(0, 0xff); //Guard, on-board
      QTouch.setGroup(1, 1);
      QTouch.setGroup(2, 1);
      QTouch.setGroup(3, 1);
      QTouch.setGroup(4, 1);
      QTouch.setGroup(5, 0xff); //on-board
      QTouch.setGroup(6, 0xff); //on-board
    }
    return 1;
  } // QTouch
  actual_touch = TOUCH_NO;
  return 0; // Error
}

void hardware_test() {
  Serial.println(F("\n **** Test function ****\nRelease button to continue."));
  while (!digitalRead(PIN_BUTTON));
  Ardudim.setCurrent(CURRENT_700);
  for (int i = 0; i <= 255; i++) {
    Ardudim.setBrightness(i);
    delay(5);
  }
  Serial.println(F("Output current: 700mA"));
  while (digitalRead(PIN_BUTTON));
  Ardudim.setCurrent(CURRENT_350);
  Ardudim.setBrightness(255);
  delay(100);
  while (!digitalRead(PIN_BUTTON));
  Serial.println(F("Output current: 350mA"));
  while (digitalRead(PIN_BUTTON));
  Ardudim.setBrightness(0);
  Serial.println(F("Done.\n"));
  default_config();
  write_config();
  delay(250);
}

void setup() {
  LED_ON;
  Serial.begin(9600);
  Serial.println(F(VERSION));

  // initialize Ardudim
  Ardudim.begin();

  //configuration
  eeprom_read_block((void*)&cfg, (void*)0, sizeof(cfg));
  if (!valid_config()) {
    Serial.println(F("Configuration invalid, using defaults"));
    default_config();
    write_config();
  }

  //here comes Ardudim
  for (int i = 0; i < CHANNELS; i++) {
    Ardudim.setCurrent(i, cfg.current[i]);
  }

  print_config();

  //measure supply voltage
  voltage.startup = 0;
  Ardudim.getVoltage(); //dummy read
  delay(1);
  for (int i = 0; i < VOLTAGE_SAMPLES; i++) {
    voltage.samples[i] = Ardudim.getVoltage() / 100;
    voltage.startup += voltage.samples[i];
  }
  voltage.startup /= 10;
  Serial.print(F("Supply voltage: "));
  print_voltage(voltage.startup);

  // Control interface (Touch, Sensor, Buttons)
  if (!init_touch()) {
    // no touch
    Serial.println(F("No control interface available."));
  }

  if (!digitalRead(PIN_BUTTON)) {
    // on-board button is pressed during start-up
    delay(100);
    LED_OFF;
    hardware_test();
  }

  //on-board button
  button.setDebounceTicks(10);
  button.setClickTicks(100);
  button.attachClick(button_click);
  button.attachDuringLongPress(button_during_long);
  button.attachLongPressStop(button_after_long);

  LED_OFF;
}


void loop() {
  static channel_t ch[] = {{cfg.brightness, 0, DIM_STOP, DIM_UP}, {cfg.brightness, 0, DIM_STOP, DIM_UP}};
  static uint8_t colortemp = cfg.colortemp, actual_colortemp = cfg.colortemp;
  static uint8_t color_direction = COLOR_STOP;
  static uint8_t prev_color_direction = COLOR_WARMER;
  static uint8_t vsens_state = VSENS_IDLE;
  static unsigned long touch_times[7];
  static unsigned long next_frame_time = 0;
  static unsigned long next_umeas_time = 0;
  static unsigned long sleep_time = 0;

  unsigned long now = millis();
  int buttons = 0;

  // AC sensor touch
  if (actual_touch == TOUCH_VSENS) {
    if (digitalRead(PIN_TOUCH)) {
      touch_times[1] = now; // last high level
      if (vsens_state == VSENS_IDLE) {
        //touched
        LED_ON;
        touch_times[0] = now; // touch down time
        vsens_state  = VSENS_ACTIVE;
      }
    } else {
      if (vsens_state == VSENS_ACTIVE) {
        if (now - touch_times[1] > VSENS_PERIOD) {
          //untouched
          LED_OFF;
          if ((now - touch_times[0] < TOUCH_LONG) && (now - touch_times[0] > TOUCH_SHORT)) {
            //short touch
            ch[0].dim_direction = DIM_STOP;
            if (ch[0].value) {
              ch[0].value = 0;
              ch[0].prev_dim_direction = DIM_DOWN;
            }
            else {
              ch[0].value = 255;
              ch[0].prev_dim_direction = DIM_UP;
            }
          }
          else {
            //long touch end
            ch[0].prev_dim_direction = ch[0].dim_direction;
            ch[0].dim_direction = DIM_STOP;
          }
          touch_times[0] = 0;
          vsens_state  = VSENS_IDLE;
        }
      }
    }
    if (vsens_state == VSENS_ACTIVE) {
      if (now - touch_times[0] > TOUCH_LONG) {
        //long touch
        if (ch[0].prev_dim_direction == DIM_UP) {
          ch[0].dim_direction = DIM_DOWN;
        }
        else {
          ch[0].dim_direction = DIM_UP;
        }
      }
    }
  } // TOUCH_VSENS

  else {
    if (actual_touch == TOUCH_BUTTONS) {
      // Get hardware button states
      buttons |= (!digitalRead(PIN_SCL) << 6) | (!digitalRead(PIN_SDA) << 5);
    }
    else if (actual_touch > 0) {
      // QTouch
      // Touch interface active
      if (!digitalRead(PIN_TOUCH)) {
        buttons = QTouch.getState();
      }
    }

    // Button actions
    for (uint8_t i = 1; i < 7; i++) {
      if (buttons & (1 << i)) {
        //button is touched
        if (touch_times[i] == 0) {
          //button just got touched
          touch_times[i] = now;
        }
        else if (now - touch_times[i] > TOUCH_LONG) {
          //button is being touched long
          if (cfg.op_mode == ONE_CHANNEL) {
            //One-channel long press
            switch (i) {
              case 1: //brighten
              case 5:
                ch[0].dim_direction = DIM_UP;
                break;
              case 2: //darken
              case 6:
                ch[0].dim_direction = DIM_DOWN;
                break;
              case 3:
                if (ch[0].prev_dim_direction == DIM_UP) {
                  ch[0].dim_direction = DIM_DOWN;
                }
                else if (ch[0].prev_dim_direction == DIM_DOWN) {
                  ch[0].dim_direction = DIM_UP;
                }
                break;
            }
          }
          if (cfg.op_mode == TWO_CHANNEL) {
            //Two-channel long press
            switch (i) {
              case 1: // CH1 brighten
                ch[0].dim_direction = DIM_UP;
                break;
              case 2: // CH1 darken
                ch[0].dim_direction = DIM_DOWN;
                break;
              case 3: // CH2 brighten
                ch[1].dim_direction = DIM_UP;
                break;
              case 4: // CH2 darken
                ch[1].dim_direction = DIM_DOWN;
                break;
              case 5: // CH1 brighten/darken
                if (ch[0].prev_dim_direction == DIM_UP) {
                  ch[0].dim_direction = DIM_DOWN;
                }
                else {
                  ch[0].dim_direction = DIM_UP;
                }
                break;
              case 6: // CH2 brighten/darken
                if (ch[1].prev_dim_direction == DIM_UP) {
                  ch[1].dim_direction = DIM_DOWN;
                }
                else {
                  ch[1].dim_direction = DIM_UP;
                }
                break;
            }
          }
          if (cfg.op_mode == TUNABLE) {
            //Tunable long press
            switch (i) {
              case 1: //brighten
                ch[0].dim_direction = DIM_UP;
                break;
              case 2: //darken
                ch[0].dim_direction = DIM_DOWN;
                break;
              case 3:
                if (ch[0].value) {
                  color_direction = COLOR_WARMER;
                }
                break;
              case 4:
                if (ch[0].value) {
                  color_direction = COLOR_COOLER;
                }
                break;
              case 5:
                if (ch[0].prev_dim_direction == DIM_UP) {
                  ch[0].dim_direction = DIM_DOWN;
                }
                else if (ch[0].prev_dim_direction == DIM_DOWN) {
                  ch[0].dim_direction = DIM_UP;
                }
                break;
              case 6:
                if (ch[0].value) {
                  if (prev_color_direction == COLOR_WARMER) {
                    color_direction = COLOR_COOLER;
                  }
                  else if (prev_color_direction == COLOR_COOLER) {
                    color_direction = COLOR_WARMER;
                  }
                }
                break;
            }
          }
        } //End long touch
      }
      else {
        //button is not touched
        if (touch_times[i] > 0) {
          //button has just been released
          if (now - touch_times[i] < TOUCH_LONG) {
            //button was clicked: short press functions
            if (cfg.op_mode == ONE_CHANNEL) {
              //One-channel short press
              ch[0].dim_direction = DIM_STOP;
              switch (i) {
                case 1: //on
                case 5:
                  ch[0].value = 255;
                  ch[0].prev_dim_direction = DIM_UP;
                  break;
                case 2: //off
                case 6:
                  ch[0].value = 0;
                  ch[0].prev_dim_direction = DIM_DOWN;
                  break;
                case 3: //on/off
                  if (ch[0].value) {
                    ch[0].value = 0;
                    ch[0].dim_direction = DIM_DOWN;
                  }
                  else {
                    ch[0].value = 255;
                    ch[0].prev_dim_direction = DIM_UP;
                  }
                  break;
              }
            }
            if (cfg.op_mode == TWO_CHANNEL) {
              //Two-channel, short press
              ch[0].dim_direction = DIM_STOP;
              ch[1].dim_direction = DIM_STOP;
              switch (i) {
                case 1: //CH1 on
                  ch[0].value = 255;
                  ch[0].prev_dim_direction = DIM_UP;
                  break;
                case 2: //CH1 off
                  ch[0].value = 0;
                  ch[0].prev_dim_direction = DIM_DOWN;
                  break;
                case 3: //CH2 on
                  ch[1].value = 255;
                  ch[1].prev_dim_direction = DIM_UP;
                  break;
                case 4: //CH2 off
                  ch[1].value = 0;
                  ch[1].prev_dim_direction = DIM_DOWN;
                  break;
                case 5: //CH1 on/off
                  if (ch[0].value) {
                    ch[0].value = 0;
                    ch[0].prev_dim_direction = DIM_DOWN;
                  }
                  else {
                    ch[0].value = 255;
                    ch[0].prev_dim_direction = DIM_UP;
                  }
                  break;
                case 6: //CH2 on/off
                  if (ch[1].value) {
                    ch[1].value = 0;
                    ch[1].prev_dim_direction = DIM_DOWN;
                  }
                  else {
                    ch[1].value = 255;
                    ch[1].prev_dim_direction = DIM_UP;
                  }
                  break;
              }
            }
            if (cfg.op_mode == TUNABLE) {
              //Tunable, short press
              uint8_t cs = colortemp / (255 / COLOR_STEPS); // nearest color step
              if (ch[0].value) {
                // only if the light is on
                switch (i) {
                  case 1: //on
                    ch[0].value = 255;
                    ch[0].prev_dim_direction = DIM_UP;
                    ch[0].dim_direction = DIM_STOP;
                    break;
                  case 2: //off
                    ch[0].value = 0;
                    ch[0].prev_dim_direction = DIM_DOWN;
                    ch[0].dim_direction = DIM_STOP;
                    break;
                  case 3: //step warmer
                    if (cs > 0) {
                      cs --;
                    }
                    colortemp = map(cs, 0, COLOR_STEPS, 0, 255);
                    prev_color_direction = COLOR_WARMER;
                    color_direction = COLOR_STOP;
                    break;
                  case 4: //step cooler
                    if (cs < COLOR_STEPS) {
                      cs ++;
                    }
                    colortemp = map(cs, 0, COLOR_STEPS, 0, 255);
                    prev_color_direction = COLOR_COOLER;
                    color_direction = COLOR_STOP;
                    break;
                  case 5: //(on),off
                    ch[0].value = 0;
                    ch[0].prev_dim_direction = DIM_DOWN;
                    ch[0].dim_direction = DIM_STOP;
                    break;
                  case 6:  // color steps
                    if (cs == 0) {
                      prev_color_direction = COLOR_COOLER;
                    }
                    else if (cs >= COLOR_STEPS) {
                      prev_color_direction = COLOR_WARMER;
                    }
                    if (prev_color_direction == COLOR_COOLER) {
                      cs += 1;
                    }
                    else {
                      cs -= 1;
                    }
                    colortemp = map(cs, 0, COLOR_STEPS, 0, 255);
                    break;
                }
              }
              else if (i != 2) {
                // Light is off.
                // Turn on, except for button 2 (off)
                ch[0].value = 255;
                ch[0].prev_dim_direction = DIM_UP;
                ch[0].dim_direction = DIM_STOP;
              }
            } // Tunable
          }
          else {
            //button was long touched
            if (color_direction) {
              prev_color_direction = color_direction;
              color_direction = COLOR_STOP;
            }
            for (uint8_t c = 0; c < 2; c++) {
              if (ch[c].dim_direction) {
                ch[c].prev_dim_direction = ch[c].dim_direction;
                ch[c].dim_direction = DIM_STOP;
              }
            }
          }
          touch_times[i] = 0; //reset button
        }
      }
    } // End of touch control
  }

  // Actual dimming with fixed frame rate
  if (now > next_frame_time) {
    if (cfg.op_mode == TWO_CHANNEL) {
      // Two channels
      for (uint8_t c = 0; c < 2; c++) {
        if (ch[c].dim_direction == DIM_UP) {
          if (ch[c].actual_level < 255) {
            ch[c].actual_level ++;
            ch[c].value = ch[c].actual_level;
          }
        }
        else if (ch[c].dim_direction == DIM_DOWN) {
          if (ch[c].actual_level > 0) {
            ch[c].actual_level --;
            ch[c].value = ch[c].actual_level;
          }
        }
        else {
          ch[c].actual_level = fade(ch[c].actual_level, ch[c].value, DIM_FADE_STEP);
        }
        Ardudim.setBrightness(c + 1, ch[c].actual_level);

        if (ch[c].value) {
          disable_sleep |= (1 << c);
        }
        else {
          disable_sleep &= ~(1 << c);
        }
      }
    }
    else {
      // One channel or tunable
      if (ch[0].dim_direction == DIM_UP) {
        if (ch[0].actual_level < 255) {
          ch[0].actual_level ++;
          ch[0].value = ch[0].actual_level;
        }
      }
      else if (ch[0].dim_direction == DIM_DOWN) {
        if (ch[0].actual_level > 0) {
          ch[0].actual_level --;
          ch[0].value = ch[0].actual_level;
        }
      }
      else {
        ch[0].actual_level = fade(ch[0].actual_level, ch[0].value, DIM_FADE_STEP);
      }
      if (ch[0].actual_level) {
        disable_sleep |= 0x3;
      }
      else {
        disable_sleep &= ~(0x3);
      }
      if (cfg.op_mode == TUNABLE) {
        if (color_direction == COLOR_COOLER) {
          if (actual_colortemp < 255) {
            actual_colortemp ++;
            colortemp = actual_colortemp;
          }
        }
        else if (color_direction == COLOR_WARMER) {
          if (actual_colortemp > 0) {
            actual_colortemp --;
            colortemp = actual_colortemp;
          }
        }
        else {
          actual_colortemp = fade(actual_colortemp, colortemp, COLOR_FADE_STEP);
        }
        if (actual_colortemp < COLORTEMP_MIN) actual_colortemp = COLORTEMP_MIN;
        else if (actual_colortemp > COLORTEMP_MAX) actual_colortemp = COLORTEMP_MAX;
        Ardudim.setTunable(ch[0].actual_level, actual_colortemp);
      }
      else {
        Ardudim.setBrightness(ch[0].actual_level);
      }
    }
    next_frame_time = now + FRAME_TIME;
  } // Actual dimming

  // Sleep while the light is off?
  if (cfg.sleep_enable == 2) {
    if (!disable_sleep) {
      if (now > sleep_time) {
        go_to_sleep();
        sleep_time = now + SLEEP_TIMEOUT;
      }
    }
    else {
      sleep_time = now + SLEEP_TIMEOUT;
    }
  }

  // Check supply voltage
  if (now > next_umeas_time) {
    voltage.samples[voltage.index] = Ardudim.getVoltage() / 100;
    if (voltage.index < VOLTAGE_SAMPLES - 1) {
      voltage.index ++;
    }
    else {
      voltage.index = 0;
    }
    uint32_t u = 0;
    for (uint8_t i = 0; i < VOLTAGE_SAMPLES; i++) {
      u += voltage.samples[i];
    }
    u /= VOLTAGE_SAMPLES;

    // over voltage?
    if (u > MAX_VOLTAGE) {
      Serial.println(F("Supply over voltage!"));
      Ardudim.setPower(0);
      delay(100);
      set_sleep_mode(SLEEP_MODE_PWR_DOWN);
      sleep_enable();
      sleep_mode();
    }
    // low voltage?
    else if (u < voltage.startup - voltage.startup / 5) {
      if (!voltage.warning) {
        Serial.println(F("Supply voltage is unstable! "));
        voltage.warning = 1;
      }
      Ardudim.setBrightness(0);
      for (uint8_t c = 0; c < 2; c++) {
        ch[c].value = 0;
        ch[c].actual_level = 0;
        ch[c].dim_direction = DIM_STOP;
        ch[c].prev_dim_direction = DIM_DOWN;
      }
    }
    else {
      voltage.warning = 0;
    }
    next_umeas_time = now + 100;
  }

  button.tick();
  menu(0);
}

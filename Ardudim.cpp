#if(ARDUINO >= 100)
#include <Arduino.h>
#else
#include <WProgram.h>
#endif
#include "Ardudim.h"
#include <avr/pgmspace.h>

/* Constructor */
ardudim::ardudim(void) {
  config.analog_min = ANALOG_MIN;
  config.current[0] = CURRENT_350;
  config.current[1] = CURRENT_350;
  config.pwm_min = PWM_MIN;
}

void ardudim::begin(void) {
  pinMode(PIN_CH1_PWM, OUTPUT);
  pinMode(PIN_CH2_PWM, OUTPUT);
  pinMode(PIN_CH1_ANA, OUTPUT);
  pinMode(PIN_CH2_ANA, OUTPUT);
  pinMode(PIN_EN_VANA, OUTPUT);
  pinMode(PIN_BUTTON, INPUT);
  digitalWrite(PIN_BUTTON, HIGH);
  pinMode(PIN_LED, OUTPUT);

  // Timer 1: 16bit Fast-PWM, 16MHz clock
  TCCR1A = (1 << WGM11);
  TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS10);
  ICR1 = 0xFFFF;
  setBrightness(0); // important in order to pre set the analog control voltage

  analogReference(INTERNAL);
  setPower(1);
}

/* Enable/disable internal 3V3 supply */
void ardudim::setPower(uint8_t state) {
  if (state) {
    digitalWrite(PIN_EN_VANA, HIGH); // Enable 3V3 supply
  }
  else {
    setBrightness(0);
    digitalWrite(PIN_EN_VANA, LOW); // Disable 3V3 supply
  }
}

/* Linear interpolation for 16 bit curves */
uint16_t get_curve16(const uint16_t curve_x[], const uint16_t curve_y[], uint16_t datapoints, uint16_t x) {
  uint16_t i = 0, lower_index, r, delta_x;
  uint32_t delta_y, m;

  while (x >= curve_x[i]) {
    i++;
    if (i == datapoints) return curve_y[datapoints - 1];
  }
  if (i) lower_index = i - 1;
  else return curve_y[0];

  if (x == curve_x[lower_index + 1]) return curve_y[lower_index + 1];

  r = (x - curve_x[lower_index]);
  delta_x = curve_x[lower_index + 1] - curve_x[lower_index];

  //m negative
  if (curve_y[lower_index] > curve_y[lower_index + 1]) {
    delta_y = (curve_y[lower_index] - curve_y[lower_index + 1]);
    m = (delta_y * 255) / delta_x;
    return curve_y[lower_index] - (m * r) / 255;
  }

  // m positive
  delta_y = (curve_y[lower_index + 1] - curve_y[lower_index]);
  m = (delta_y * 255) / delta_x;
  return curve_y[lower_index] + (m * r) / 255;
}

/*  Set one channel to a linear 16 bit dimming value
    Channel: 1 or 2, value 0 - 0xffff
*/
void ardudim::setChannel(uint8_t channel, uint16_t value) {
  uint8_t pwm;
  uint16_t analog;
  // Only channel 1 and 2 is valid
  if ((channel == 0) || (channel > CHANNELS)) {
    return;
  }
  // Get absolute dimming value
  value = map(value, 0, 0xffff, 0, config.current[channel - 1]);
  if (value == 0) {
    // Channel off
    pwm = 0;
    analog = config.analog_min;
  }
  else if (value < config.analog_min) {
    // Below PWM threshold
    pwm = map(value, 0, config.analog_min, 0, 0xff);
    if (pwm > 0 && pwm < config.pwm_min) {
      pwm = config.pwm_min;
    }
    analog = config.analog_min;
  }
  else {
    // Above PWM threshold
    pwm = 0xff;
    analog = value;
  }
  // Linearity correction and inversion
  analog = 0xffff - get_curve16(i_curve_x, i_curve_y, I_CURVE_DATAPOINTS, analog);
  switch (channel) {
    case 1:
      analogWrite(PIN_CH1_ANA, analog);
      analogWrite(PIN_CH1_PWM, pwm);
      break;
    case 2:
      analogWrite(PIN_CH2_ANA, analog);
      analogWrite(PIN_CH2_PWM, pwm);
      break;
  }
}

/* Set brightness and color balance */
void ardudim::setTunable(uint8_t brightness, uint8_t colortemp) {
  uint16_t linear = pgm_read_word_near(dimcurve + brightness);
  uint16_t cool = map(colortemp, 0, 255, 0, linear);
  setChannel(1, linear - cool); //warm-white channel
  setChannel(2, cool);          //cool-white channel
}

/* Set brightness for all channels */
void ardudim::setBrightness(uint8_t value) {
  uint16_t linear = pgm_read_word_near(dimcurve + value);
  setChannel(1, linear);
  setChannel(2, linear);
}

/* Set brightness for one channel */
void ardudim::setBrightness(uint8_t channel, uint8_t value) {
  uint16_t linear = pgm_read_word_near(dimcurve + value);
  setChannel(channel, linear);
}

/* Sets the maximum LED current all channels */
void ardudim::setCurrent(uint16_t current) {
  for (uint8_t i = 0; i < CHANNELS; i++) {
    config.current[i] = current;
  }
}

/* Sets the maximum LED current for one channel */
void ardudim::setCurrent(uint8_t channel, uint16_t current) {
  if (channel < CHANNELS) {
    config.current[channel] = current;
  }
}

/* Returns the input voltage in mV */
uint16_t ardudim::getVoltage(void) {
  /* Input voltage divider: 244k7:4k7 = 52.06:1 -> 1V input = 19,2mV on ADC input
     ADC reference: 1.1V
     1100mV / 1024 = 1.07mV ADC resolution
     1.07mV * 52.06 = 56mV input resolution
     1023 * 56mV = 57288mV full scale
  */
  return map(analogRead(PIN_VIN), 0, 1023, 0, 57288);
}

/*  Set the transition point between analog and PWM dimming
    Value: linear 16 bit
    The new value is considered on the next channel update.
*/
void ardudim::setAnalogMin(uint16_t value) {
  config.analog_min = value;
}

/*  Set the lowest PWM dimming value
    Value: linear 8 bit
    The new value is considered on the next channel update.
*/
void ardudim::setPwmMin(uint8_t value) {
  config.pwm_min = value;
}

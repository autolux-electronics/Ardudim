#include <Ardudim.h>

ardudim Ardudim;

void setup() {
  Ardudim.begin();

  /*  Set LED driver maximum output current for both channels.
   *  It is recommended to use provided defines:
   *  CURRENT_350, CURRENT_500 and CURRENT_700
   *  Default is 350mA
   */
  Ardudim.setCurrent(CURRENT_350);

  // Turn on on-board LED
  LED_ON;
}

void loop() {
  // Channel 1 on, channel 2 off:
  Ardudim.setBrightness(1, 255);
  Ardudim.setBrightness(2, 0);
  delay(1000);

  // Channel 1 off, channel 2 on:
  Ardudim.setBrightness(1, 0);
  Ardudim.setBrightness(2, 255);
  delay(1000);
}

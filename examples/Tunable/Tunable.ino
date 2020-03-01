/* This example demonstrates how to use tunable white on the ALX-ARDUDIM-CC.
   Connect warm-white LED to channel 1 and cool white LED to channel 2.
*/
#include <Ardudim.h>

ardudim Ardudim;

void setup() {
  Ardudim.begin();

  /*  Set LED driver maximum output current for both channels.
      It is recommended to use provided defines:
      CURRENT_350, CURRENT_500 and CURRENT_700
      Default is 350mA
  */
  Ardudim.setCurrent(CURRENT_350);

  // Turn on on-board LED
  LED_ON;
}

void loop() {
  uint8_t brightness = 255;
  uint8_t colortemp;

  // fade from warm to cool-white
  for (colortemp = 0; colortemp < 255; colortemp++) {
    Ardudim.setTunable(brightness, colortemp);
    delay(20);
  }

  // and back to warm-white
  for (colortemp = 255; colortemp > 0; colortemp--) {
    Ardudim.setTunable(brightness, colortemp);
    delay(20);
  }
}

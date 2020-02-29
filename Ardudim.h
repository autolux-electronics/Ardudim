#ifndef ARDUDIM_H
#define ARDUDIM_H

#if(ARDUINO >= 100)
#include <Arduino.h>
#else
#include <WProgram.h>
#endif

/* Pins */
#define PIN_LED       13  // Status LED, active high
#define PIN_EN_VANA   8   // 3,3V analog supply enable
#define PIN_CH1_PWM   6   // PWM control for channel 1
#define PIN_CH1_ANA   10  // Analog current control for channel 1 (hw inverted)
#define PIN_CH2_PWM   5   // PWM control for channel 2
#define PIN_CH2_ANA   9   // Analog current control for channel 2 (hw inverted)
#define PIN_VIN       A2  // Supply voltage through 52.06:1 voltage divider
#define PIN_BUTTON    7   // On-board button, active low

/* on-board LED */
#define LED_ON  digitalWrite(PIN_LED, HIGH)
#define LED_OFF digitalWrite(PIN_LED, LOW)

/* Number of hardware channels */
#define CHANNELS      2

/* Modes */
#define ONE_CHANNEL   1   // Both channels linked
#define TWO_CHANNEL   2   // Individual channel control
#define TUNABLE       3   // Tunable white

/* Pre defined LED driver currents
    65535 = 700mA as defined by the linearity curve
*/
#define CURRENT_20    1872
#define CURRENT_50    4681
#define CURRENT_100   9362
#define CURRENT_350   32768
#define CURRENT_500   46811
#define CURRENT_700   65535

/* Analog driver current linearity */
#define I_CURVE_DATAPOINTS 7
const uint16_t i_curve_x[I_CURVE_DATAPOINTS] = {0, CURRENT_20, CURRENT_50, CURRENT_100, CURRENT_350, CURRENT_500, CURRENT_700};
const uint16_t i_curve_y[I_CURVE_DATAPOINTS] = {0, 8650,       14550,      22857,       42400,       51350,       61250};

/* Default transition between analog dimming and PWM
    100% linear = 65535
*/
#define ANALOG_MIN 1966 // 3%

/* Minium PWM value (0 ... 255) */
#define PWM_MIN 5 // 5/255 = 2% of 3% = 0,005% minimum dimming level

/* Gamma 2.2 dimming curve */
const uint16_t dimcurve[256] PROGMEM =
{0, 1, 2, 4, 8, 12, 18, 25, 33, 42, 53, 66, 79, 94, 111, 129, 149, 170, 193, 217, 243, 270, 299, 330, 362, 396, 432, 469, 508, 549, 592, 636, 682, 730, 779, 830, 883, 938, 995, 1053, 1114, 1176, 1240, 1306, 1374, 1443, 1515, 1588, 1663, 1740, 1819, 1900, 1983, 2068, 2155, 2244, 2334, 2427, 2522, 2618, 2717, 2818, 2920, 3025, 3131, 3240, 3351, 3463, 3578, 3695, 3814, 3935, 4058, 4183, 4310, 4439, 4570, 4703, 4839, 4976, 5116, 5258, 5402, 5547, 5696, 5846, 5998, 6153, 6309, 6468, 6629, 6792, 6957, 7125, 7295, 7466, 7640, 7816, 7995, 8175, 8358, 8543, 8730, 8920, 9111, 9305, 9501, 9700, 9900, 10103, 10308, 10515, 10725, 10936, 11151, 11367, 11585, 11806, 12029, 12255, 12482, 12712, 12945, 13179, 13416, 13655, 13897, 14141, 14387, 14635, 14886, 15139, 15394, 15652, 15912, 16175, 16439, 16706, 16976, 17248, 17522, 17798, 18077, 18358, 18642, 18928, 19216, 19507, 19800, 20096, 20394, 20694, 20997, 21302, 21609, 21919, 22232, 22546, 22863, 23183, 23505, 23829, 24156, 24485, 24817, 25151, 25488, 25827, 26168, 26512, 26858, 27207, 27558, 27912, 28268, 28627, 28988, 29352, 29718, 30086, 30457, 30831, 31207, 31585, 31966, 32350, 32736, 33124, 33515, 33908, 34304, 34703, 35104, 35507, 35913, 36322, 36733, 37146, 37562, 37981, 38402, 38826, 39252, 39681, 40112, 40546, 40982, 41421, 41863, 42307, 42754, 43203, 43654, 44109, 44566, 45025, 45487, 45952, 46419, 46889, 47361, 47836, 48313, 48793, 49276, 49761, 50249, 50739, 51233, 51728, 52226, 52727, 53231, 53737, 54246, 54757, 55271, 55787, 56306, 56828, 57353, 57880, 58409, 58942, 59477, 60014, 60554, 61097, 61643, 62191, 62742, 63295, 63851, 64410, 64971, 65535};

/* Global configuration */
typedef struct {
  uint16_t analog_min;          // Transition point between analog and PWM dimming
  uint16_t current[CHANNELS];   // output current at 100% for each channel
  uint8_t pwm_min;              // minimum PWM dimming level
} ardudim_config;

class ardudim
{
  public:
    ardudim();
    void begin();
    void setCurrent(uint16_t current);
    void setCurrent(uint8_t channel, uint16_t current);
    void setBrightness(uint8_t value);
    void setBrightness(uint8_t channel, uint8_t value);
    void setTunable(uint8_t brightness, uint8_t colortemp);
    void setChannel(uint8_t channel, uint16_t value);
    void setPower(uint8_t state);
    uint16_t getVoltage(void);
    void setAnalogMin(uint16_t value);
    void setPwmMin(uint8_t value);

  private:
    ardudim_config config;
};

#endif

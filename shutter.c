#include <stdint.h>

#include "hardware/pwm.h"

#define SHUTTER_PWM_DUTY ((uint16_t) 20000)
#define SHUTTER_PWM_SYSCLK_DIV ((uint8_t) 125)
#define SHUTTER_PWM_FREQUENCY  50U

#define SHUTTER_OFF_PWM       600
#define SHUTTER_ON_PWM        2350                // FS90 Micro Servo
                                                  // Docs: https://holzcoredump.cc/FITEC_FS90.pdf
#define MAX_SHUTTER_MS 15000

#define SHUTTER_DELAY_MS  250                     // delay for shutter to get to right position

#define SHUTTER_START_UNSTABLE    0
#define SHUTTER_END_UNSTABLE      1
#define SHUTTER_START_STABLE      2
#define SHUTTER_END_STABLE        3
#define SHUTTER_ON_OPTION         4
#define SHUTTER_OFF_OPTION        5

#define SHUTTER_PIN 0
#define SHUTTER_PIN_SLICE pwm_gpio_to_slice_num(SHUTTER_PIN)

typedef struct {
  absolute_time_t deadline;
  uint16_t        time_ms;
  uint8_t         intern_state;
} shutter_context_t;

void
do_shutter_work(shutter_context_t *shutter)
{
  if(shutter->time_ms != 0){
    const bool deadline_met = get_absolute_time() > shutter->deadline;
    if(deadline_met){
      switch(shutter->intern_state){
        case SHUTTER_START_UNSTABLE:
          pwm_set_gpio_level(SHUTTER_PIN, SHUTTER_ON_PWM);
          pwm_set_enabled(SHUTTER_PIN_SLICE, true);
          shutter->intern_state = SHUTTER_END_UNSTABLE;
          shutter->deadline = make_timeout_time_ms(SHUTTER_DELAY_MS);
          break;
        case SHUTTER_END_UNSTABLE:
          pwm_set_enabled(SHUTTER_PIN_SLICE, false);
          shutter->intern_state = SHUTTER_START_STABLE;
          shutter->deadline = make_timeout_time_ms(shutter->time_ms);
          break;
        case SHUTTER_START_STABLE:
          pwm_set_gpio_level(SHUTTER_PIN, SHUTTER_OFF_PWM);
          pwm_set_enabled(SHUTTER_PIN_SLICE, true);
          shutter->intern_state = SHUTTER_END_STABLE;
          shutter->deadline = make_timeout_time_ms(SHUTTER_DELAY_MS);
          break;
        case SHUTTER_END_STABLE:
          pwm_set_enabled(SHUTTER_PIN_SLICE, false);
          shutter->intern_state = SHUTTER_START_UNSTABLE;
          shutter->time_ms = 0;
          break;
        case SHUTTER_ON_OPTION:
          pwm_set_gpio_level(SHUTTER_PIN, SHUTTER_ON_PWM);
          pwm_set_enabled(SHUTTER_PIN_SLICE, true);
          shutter->intern_state = SHUTTER_END_STABLE;
          shutter->deadline = make_timeout_time_ms(SHUTTER_DELAY_MS);
          break;
        case SHUTTER_OFF_OPTION:
          pwm_set_gpio_level(SHUTTER_PIN, SHUTTER_OFF_PWM);
          pwm_set_enabled(SHUTTER_PIN_SLICE, true);
          shutter->intern_state = SHUTTER_END_STABLE;
          shutter->deadline = make_timeout_time_ms(SHUTTER_DELAY_MS);
          break;
      }
    }
  }
}

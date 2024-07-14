#pragma once

#define SHUTTER_PWM_DUTY       ((uint16_t) 20000)
#define SHUTTER_PWM_SYSCLK_DIV ((uint8_t)  125)

#define SHUTTER_PIN       0
#define SHUTTER_PIN_SLICE pwm_gpio_to_slice_num(SHUTTER_PIN)

#define SHUTTER_ON_OPTION  4
#define SHUTTER_OFF_OPTION 5
#define MAX_SHUTTER_MS     15000

typedef struct {
  absolute_time_t deadline;
  uint16_t        time_ms;
  uint8_t         intern_state;
} shutter_context_t;

void
do_shutter_work(shutter_context_t*);

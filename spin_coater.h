#pragma once

#if CONFIG_SPIN_COATER
#if CONFIG_SPIN_COATER != CONFIG_SPIN_COATER_DSHOT && CONFIG_SPIN_COATER != CONFIG_SPIN_COATER_PWM
#error "Invalid spin coater value type specified. Either 'dshot' or 'pwm' has to be enabled"
#endif
#endif

#include <stdint.h>
#include <stdio.h>
#include "pico/time.h"
#if CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_DSHOT
#include "spin_coater_dshot.h"
#elif CONFIG_SPIN_COATER ==  CONFIG_SPIN_COATER_PWM
#include "spin_coater_pwm.h"
#endif

#define SPIN_COATER_MAX_RPM_VALUE 6000

typedef enum
{
  SPIN_IDLE = 0,
  SPIN_STARTED_WITH_TIMER,
  SPIN_STARTED_WITH_FORCE_VALUE,
  SPIN_SMOOTH_STOP_REQUESTED,
} spin_state_t;

typedef struct
{
#if CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_PWM
  uint32_t pwm_duty;
  unsigned int pwm_slice_num;
#elif CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_DSHOT
  uint32_t dshot_throttle_val;
#endif
  uint32_t rpm_speedup_update_delay;
  uint32_t rpm_slowdown_update_delay;
  uint32_t set_rpm;
  spin_state_t spin_state;
  alarm_id_t timer_id;
} spin_coater_context_t;

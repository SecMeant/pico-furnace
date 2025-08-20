#pragma once

#define CONFIG_SPIN_COATER_DSHOT_ANY (\
    CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_DSHOT || \
    CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_DSHOT_WITH_TELEMETRY \
)

#ifdef CONFIG_SPIN_COATER
#if CONFIG_SPIN_COATER != CONFIG_SPIN_COATER_DSHOT && CONFIG_SPIN_COATER != CONFIG_SPIN_COATER_DSHOT_WITH_TELEMETRY && CONFIG_SPIN_COATER != CONFIG_SPIN_COATER_PWM
#error "Invalid spin coater value type specified. Either 'dshot', 'bidshot' or 'pwm' has to be enabled"
#endif
#endif

#include <stdint.h>
#include <stdio.h>
#include "pico/time.h"
#if CONFIG_SPIN_COATER_DSHOT_ANY
#include "spin_coater_dshot.h"
#elif CONFIG_SPIN_COATER ==  CONFIG_SPIN_COATER_PWM
#include "spin_coater_pwm.h"
#endif

#define SPIN_COATER_MAX_RPM_VALUE 6000
#define DSHOT_RAW_DATA_SMOOTHING_BUFFER_SIZE 300

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
#elif CONFIG_SPIN_COATER_DSHOT_ANY
  uint32_t dshot_throttle_val;
#endif
  uint32_t current_rpm;
  uint32_t rpm_speedup_update_delay;
  uint32_t rpm_slowdown_update_delay;
  uint32_t set_rpm;
  spin_state_t spin_state;
  alarm_id_t spin_timer;
  struct repeating_timer telemetry_collector_timer;
  uint32_t dshot_raw_data_smoothing_buffer[DSHOT_RAW_DATA_SMOOTHING_BUFFER_SIZE];
  uint32_t dshot_raw_data_smoothing_buffer_idx;
  uint32_t dshot_raw_data_smoothing_buffer_current_size;
} spin_coater_context_t;

#if CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_DSHOT_WITH_TELEMETRY
bool repeating_dshot_collect_data_callback(__unused struct repeating_timer *t);

void
dshot_recv_telemetry(spin_coater_context_t *ctx);

size_t
calc_avg_from_raw_dshot_buffer(spin_coater_context_t *ctx);

void reset_dshot_buffers(spin_coater_context_t *ctx);

#endif
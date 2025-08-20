#include "spin_coater.h"
#include "stdint.h"
#include "hardware/timer.h"

#if CONFIG_SPIN_COATER != CONFIG_SPIN_COATER_DSHOT_WITH_TELEMETRY
#define NUMBER_OF_HOLES 2
#define MICROSECONDS_PER_MINUTE 60000000
static uint32_t rpm_counter_based_on_photo_sensor;
void
rpm_photo_sensor_feedback_irq_handler(unsigned int gpio, uint32_t events)
{
  static uint32_t count_of_measurements = 0;
  static uint32_t last_time = 0;

  if (count_of_measurements == 0) {
    last_time = time_us_32();
    count_of_measurements++;
    return;
  }

  if (count_of_measurements < NUMBER_OF_HOLES) {
    count_of_measurements++;
    return;
  }

  const uint32_t current_time_us = time_us_32();
  const uint32_t time_elapsed_us = current_time_us - last_time;
  if((MICROSECONDS_PER_MINUTE / time_elapsed_us) < rpm_counter_based_on_photo_sensor + 1000)
    rpm_counter_based_on_photo_sensor = (MICROSECONDS_PER_MINUTE / time_elapsed_us);
  count_of_measurements = 0;
}
#endif

#if  CONFIG_SPIN_COATER_DSHOT_ANY
int
set_dshot_safe(furnace_context_t *ctx, unsigned dshot)
{
  if (dshot < SPIN_COATER_MIN_THROTTLE_COMMAND || dshot > SPIN_COATER_MAX_THROTTLE_COMMAND)
    return 1;

  ctx->spin_coater.dshot_throttle_val = dshot;
  DEBUG_printf("\n DSHOT set to: %lu!\n", dshot);
  dshot_send_command(ctx->spin_coater.dshot_throttle_val);
  return 0;
}
#elif CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_PWM
int
set_spin_coater_pwm_safe(furnace_context_t* ctx, unsigned duty)
{
  if (duty < SPIN_COATER_PWM_490_FREQ_DUTY_MIN || duty > SPIN_COATER_PWM_490_FREQ_DUTY_MAX)
    return 1;

  ctx->spin_coater.pwm_duty = duty;
  DEBUG_printf("\n PWM set to: %lu!\n", duty);
  return spinpwm_set_freq_duty(ctx->spin_coater.pwm_slice_num, PWM_CHAN_A, SPIN_COATER_PWM_490_FREQ, duty);
}
#endif
// TODO refactor this code to support PWM smooth transition in future, right now it's not that important
// cause PWM is not very usefull
// We can consider dropping support for PWM in future.
#if CONFIG_SPIN_COATER_DSHOT_ANY
static inline int
scale_dshot_value_when_speeding(furnace_context_t* ctx, int rpm_left)
{
    return (15.0 / ctx->spin_coater.set_rpm) * rpm_left;
}

static inline int
scale_dshot_value_when_stopping(furnace_context_t* ctx, int rpm_left)
{
    return 15.0 - (15.0 / ctx->spin_coater.set_rpm) * rpm_left;
}

static absolute_time_t
do_dshot_smooth_transition(furnace_context_t* ctx)
{
#if CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_DSHOT_WITH_TELEMETRY
  ctx->spin_coater.current_rpm = calc_avg_from_raw_dshot_buffer(&ctx->spin_coater);
#else 
  ctx->spin_coater.current_rpm = rpm_counter_based_on_photo_sensor;
#endif
  const int direction = ctx->spin_coater.current_rpm < ctx->spin_coater.set_rpm ? 1 : -1;
  const int rpm_diff_abs = abs(ctx->spin_coater.set_rpm - ctx->spin_coater.current_rpm);
  absolute_time_t next_delay;
  if(SPIN_STARTED_WITH_TIMER == ctx->spin_coater.spin_state) {
    if(rpm_diff_abs > 200) {
      ctx->spin_coater.dshot_throttle_val += scale_dshot_value_when_speeding(ctx,
                                                                            rpm_diff_abs);
      next_delay = make_timeout_time_ms(ctx->spin_coater.rpm_speedup_update_delay);
    } else if (rpm_diff_abs > 25 ){
      ctx->spin_coater.dshot_throttle_val += direction;
      next_delay = make_timeout_time_ms(150);
    } else if (rpm_diff_abs > 10 ){
      ctx->spin_coater.dshot_throttle_val += direction;
      next_delay = make_timeout_time_ms(200);
    }else{
      next_delay = make_timeout_time_ms(ctx->spin_coater.rpm_speedup_update_delay);
    }
    
  }
  else if(SPIN_SMOOTH_STOP_REQUESTED == ctx->spin_coater.spin_state) {
    ctx->spin_coater.dshot_throttle_val -= 5;
      if(rpm_diff_abs > (ctx->spin_coater.set_rpm - 250) ||
          ctx->spin_coater.dshot_throttle_val < SPIN_COATER_MIN_THROTTLE_COMMAND) {
        ctx->spin_coater.dshot_throttle_val = SPIN_COATER_MIN_THROTTLE_COMMAND;
        ctx->spin_coater.spin_state = SPIN_IDLE;
        ctx->spin_coater.current_rpm = 0;
#if CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_DSHOT_WITH_TELEMETRY
        cancel_repeating_timer(&ctx->spin_coater.telemetry_collector_timer);
        reset_dshot_buffers(&ctx->spin_coater);
#endif
      }
    next_delay = make_timeout_time_ms(ctx->spin_coater.rpm_slowdown_update_delay);
  } else {
   next_delay = make_timeout_time_ms(1000); 
  }
  set_dshot_safe(ctx, ctx->spin_coater.dshot_throttle_val);
  return next_delay;
}
#elif CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_PWM
static absolute_time_t
do_pwm_smooth_transition(furnace_context_t* ctx)
{
  int direction = ctx->spin_coater.current_rpm < ctx->spin_coater.set_rpm ? 1 : -1;

  ctx->spin_coater.pwm_duty += direction;
  set_spin_coater_pwm_safe(ctx, ctx->spin_coater.pwm_duty);

  return make_timeout_time_ms(100);
}
#endif

#if CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_DSHOT_WITH_TELEMETRY
bool repeating_dshot_collect_data_callback(__unused struct repeating_timer *t) {
    dshot_recv_telemetry((spin_coater_context_t*)t->user_data);
    return true;
}
#endif

static void
do_spin_coater_throttle_value_update(furnace_context_t *ctx, bool deadline_met)
{
    if (deadline_met) {
        if ((ctx->spin_coater.spin_state == SPIN_STARTED_WITH_TIMER) ||
            (ctx->spin_coater.spin_state == SPIN_SMOOTH_STOP_REQUESTED)) {
#if CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_PWM
          ctx->spin_coater_throttle_value_update_deadline = do_pwm_smooth_transition(ctx);
#elif CONFIG_SPIN_COATER_DSHOT_ANY
          ctx->spin_coater_throttle_value_update_deadline = do_dshot_smooth_transition(ctx);
#endif
        } else if(ctx->spin_coater.spin_state == SPIN_STARTED_WITH_FORCE_VALUE) {
#if CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_DSHOT_WITH_TELEMETRY
          ctx->spin_coater.current_rpm = calc_avg_from_raw_dshot_buffer(&ctx->spin_coater);
#else
          ctx->spin_coater.current_rpm = rpm_counter_based_on_photo_sensor;
#endif
          ctx->spin_coater_throttle_value_update_deadline = make_timeout_time_ms(200);
        } else {
          ctx->spin_coater_throttle_value_update_deadline = make_timeout_time_ms(200);
        }
    }
}

void
init_spin_coater(furnace_context_t *ctx)
{
#if CONFIG_SPIN_COATER != CONFIG_SPIN_COATER_DSHOT_WITH_TELEMETRY
  gpio_set_irq_enabled_with_callback(
    CONFIG_SPIN_COATER_RPM_FEEDBACK_PIN,
    GPIO_IRQ_EDGE_FALL,
    true,
    &rpm_photo_sensor_feedback_irq_handler);
#endif

#if CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_PWM
  // Tell GPIO 0 and 1 they are allocated to the PWM
  gpio_set_function(CONFIG_SPIN_COATER_ENGINE_CONTROL_PIN, GPIO_FUNC_PWM);
  // Find out which PWM slice is connected to GPIO 0 (it's slice 0)
  uint slice_num = pwm_gpio_to_slice_num(CONFIG_SPIN_COATER_ENGINE_CONTROL_PIN);
  // Set channel A output high for one cycle before dropping
  spinpwm_set_freq_duty(slice_num, PWM_CHAN_A, SPIN_COATER_PWM_490_FREQ, PWM_IDLE_DUTY);
  // Set initial B output high for three cycles before dropping
  // Set the PWM running
  pwm_set_enabled(slice_num, true);
#elif CONFIG_SPIN_COATER_DSHOT_ANY
    dshot_init(CONFIG_SPIN_COATER_ENGINE_CONTROL_PIN, CONFIG_SPIN_COATER_RPM_FEEDBACK_PIN);
    dshot_send_command(SPIN_COATER_MIN_THROTTLE_COMMAND);
#if CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_DSHOT_WITH_TELEMETRY
    reset_dshot_buffers(&ctx->spin_coater);
#endif
#endif

#if CONFIG_SPIN_COATER == CONFIG_SPIN_COATER_PWM
  ctx->spin_coater.pwm_slice_num = slice_num;
  ctx->spin_coater.pwm_duty = PWM_IDLE_DUTY;
#elif CONFIG_SPIN_COATER_DSHOT_ANY
  ctx->spin_coater.dshot_throttle_val = SPIN_COATER_MIN_THROTTLE_COMMAND;
#endif
  ctx->spin_coater.set_rpm = 0;
  ctx->spin_coater.current_rpm = 0;
  ctx->spin_coater.spin_state = SPIN_IDLE;
  ctx->spin_coater.rpm_speedup_update_delay = CONFIG_SPIN_COATER_RPM_UPDATE_DEFAULT_DELAY;
  ctx->spin_coater.rpm_slowdown_update_delay = CONFIG_SPIN_COATER_RPM_UPDATE_DEFAULT_DELAY;
  ctx->spin_coater_throttle_value_update_deadline = make_timeout_time_ms(CONFIG_SPIN_COATER_RPM_UPDATE_DEFAULT_DELAY);
}
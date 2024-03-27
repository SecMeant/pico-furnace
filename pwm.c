#include <stdint.h>
#include "hardware/pwm.h"
#include "hardware/platform_defs.h"

#include "common.h" // for MAX_PWM

#define PWM_DUTY ((uint16_t) 62500)
#define PWM_SYSCLK_DIV ((uint8_t) 250)

#define PWM_LEVEL_SCALE (PWM_DUTY / MAX_PWM)
_Static_assert(PWM_DUTY % MAX_PWM == 0);

static unsigned
pwm_scale_level(unsigned unscaled_pwm)
{
  return unscaled_pwm * PWM_LEVEL_SCALE;
}

static inline int
set_pwm_safe(unsigned pin, furnace_context_t *ctx, unsigned new_pwm)
{
  if (new_pwm > MAX_PWM)
    return 1;

  if (((unsigned) ((uint16_t) new_pwm)) != new_pwm)
    return 1;

  switch(pin){
#if CONFIG_WATER
    case WATER_PIN:
      ctx->pwm_water = new_pwm;

      pwm_set_gpio_level(WATER_PIN, new_pwm);
      break;
#endif

    case FURNACE_FIRE_PIN:
      ctx->pwm_level = new_pwm;

      const unsigned new_pwm_scaled = pwm_scale_level(new_pwm);
      pwm_set_gpio_level(FURNACE_FIRE_PIN, new_pwm_scaled);
      break;

    default:
      return -1;
  }

  return 0;
}


/*
 * There is no way to set the frequency of the PWM directly, but we can use
 * sys_clk divider and duty cycle with properly scaled pwm level before being
 * set to get desired frequency defined PWM.
 *
 * We choose 8Hz, because our SSR-based power supply circuitry can't output a
 * proper sine wave when driven by higher frequency. There is something wrong
 * with the hardware driving the PWM signal. For w workaround we just lower the
 * frequency to 8Hz which was observed to give much better signal, despite
 * refreshing at a slower rate.
 */
static void
pwm_config_freq(pwm_config *cfg)
{
  _Static_assert(SYS_CLK_KHZ * 1000U == PWM_DUTY * PWM_SYSCLK_DIV * 8U);

  pwm_config_set_wrap(cfg, PWM_DUTY);
  pwm_config_set_clkdiv_int(cfg, PWM_SYSCLK_DIV);
  pwm_config_set_clkdiv_mode(cfg, PWM_DIV_FREE_RUNNING);
}

static void
init_pwm(void)
{
  /* Setup GPIO for PWM and disable heating. */

  gpio_set_function(FURNACE_FIRE_PIN, GPIO_FUNC_PWM);
  pwm_set_irq_enabled(FURNACE_FIRE_PWM_SLICE, false);

  pwm_config cfg = pwm_get_default_config();
  pwm_config_freq(&cfg);

  pwm_set_gpio_level(FURNACE_FIRE_PIN, 0);
  pwm_init(FURNACE_FIRE_PWM_SLICE, &cfg, true);

#if CONFIG_WATER
  gpio_set_function(WATER_PIN, GPIO_FUNC_PWM);
  pwm_set_irq_enabled(WATER_PIN_SLICE, false);

  pwm_config water_cfg = pwm_get_default_config();
  pwm_config_set_wrap(&water_cfg, MAX_PWM);

  pwm_set_gpio_level(WATER_PIN, 0);
  pwm_init(WATER_PIN_SLICE, &water_cfg, true);
#endif

#if CONFIG_SHUTTER
  gpio_set_function(SHUTTER_PIN, GPIO_FUNC_PWM);
  pwm_set_irq_enabled(SHUTTER_PIN_SLICE, false);

  pwm_config shutter_cfg = pwm_get_default_config();
  pwm_config_set_wrap(&shutter_cfg, SHUTTER_PWM_DUTY);
  pwm_config_set_clkdiv_int(&shutter_cfg, SHUTTER_PWM_SYSCLK_DIV);
  pwm_config_set_clkdiv_mode(&shutter_cfg, PWM_DIV_FREE_RUNNING);

  pwm_set_gpio_level(SHUTTER_PIN, 0);
  pwm_init(SHUTTER_PIN_SLICE, &shutter_cfg, false);
#endif
}

#include <stdint.h>
#include "hardware/pwm.h"
#include "hardware/platform_defs.h"

#include "common.h" // for MAX_PWM

#define PWM_DUTY ((uint16_t) 12500)
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
 * set to get 40Hz PWM.
 *
 * We choose 40Hz because our SSR support max 50Hz and we just want to be safe.
 */
static void
pwm_config_freq_40hz(pwm_config *cfg)
{
  _Static_assert(SYS_CLK_KHZ * 1000U == PWM_DUTY * PWM_SYSCLK_DIV * 40U);

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
  pwm_config_freq_40hz(&cfg);

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
}


#include <stdint.h>
#include "hardware/pwm.h"
#include "hardware/platform_defs.h"

#define MAX_PWM 50U
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
set_pwm_safe(furnace_context_t *ctx, unsigned new_pwm)
{
  if (new_pwm > MAX_PWM)
    return 1;

  if (((unsigned) ((uint16_t) new_pwm)) != new_pwm)
    return 1;

  ctx->pwm_level = new_pwm;

  const unsigned new_pwm_scaled = pwm_scale_level(new_pwm);
  pwm_set_gpio_level(FURNACE_FIRE_PIN, new_pwm_scaled);

  /*
   * We disable the PWM for max possible value to get rid of the annoying
   * "noise" making MAX_PWM going down to 0V for a moment at the end of the duty
   * cycle. The trick is to set the level first, on the running pwm and then
   * disable the pwm to "lock in" the value. The value set by the
   * pwm_set_gpio_level is latched on the next wrap of the pwm. We have to make
   * sure we latched it before it being stoped. Otherwise pwm_set_enabled could
   * have been called before the wrap happend and value set by
   * pwm_set_gpio_level would have been ignored.
   */
  _Static_assert(PWM_DUTY / MAX_PWM == PWM_LEVEL_SCALE);
  if (new_pwm == MAX_PWM) {
    /* Make sure the pwm_set_gpio_level value was latched. */
    while (pwm_get_counter(FURNACE_FIRE_PWM_SLICE) != 0);

    /* And finally disable the PWM to get constant 3.3V output. */
    pwm_set_enabled(FURNACE_FIRE_PWM_SLICE, false);
  }

  /* For any other value we enable the PWM. */
  else {
    pwm_set_enabled(FURNACE_FIRE_PWM_SLICE, true);
  }

  return 0;
}

/*
 * There is no way to set the frequency of the PWM directly, but we can use
 * sys_clk divider and duty cycle with properly scaled pwm level before being
 * set to get 40Hz PWM.
 *
 * We choose 40Hz because our SSR support max 50Hz and we just want to mbe safe.
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
}


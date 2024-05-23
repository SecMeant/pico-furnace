#pragma once

#include "hardware/pwm.h"

#define SPIN_COATER_PWM_490_FREQ 490
#define SPIN_COATER_PWM_490_FREQ_DUTY_MIN 49
#define SPIN_COATER_PWM_490_FREQ_DUTY_MAX 98
#define PWM_IDLE_DUTY SPIN_COATER_PWM_490_FREQ_DUTY_MIN
/* Normaly IDLE state for the engine is 49% pwm duty
   but since the engine is under load it starts spining from 53 %
   instead of 49 % pwm duty cycle. This variable was added to speed up process
   of spining up the engine in automatic mode with timer */
#define PWM_HEAVY_LOADED_IDLE_DUTY 52

int
pwm_set_freq_duty(uint32_t slice_num,
                  uint32_t chan,
                  uint32_t freq,
                  int duty_cycle);
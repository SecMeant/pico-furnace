#ifndef PID_H_
#define PID_H_
#include <stdint.h>
#include <stdbool.h>

#define MAX_PWM 255
#define OFFSET 127
#define MAX_TEMP 1210

#define PWM_MEM_SIZE 10
#define ERR_MEM_SIZE 32 

struct pid_ctx{
  unsigned p_const;
  unsigned i_const;
  unsigned d_const;

  int      err_mem[ERR_MEM_SIZE];
  int      err_count;

  uint8_t  last_pwms[PWM_MEM_SIZE];
  int      pwm_count;

  unsigned des_value;
  bool     in_falling_mode;
  bool     is_open;
  bool     safeguard_enabled;

  uint8_t  des_pwm;
};

struct pid_ctx *init_pid(unsigned p_const,unsigned i_const,unsigned d_const, int temp);

uint8_t calculate_pwm(struct pid_ctx *pid, int measure);

uint8_t safeguard(struct pid_ctx *pid, uint8_t des_pwm, int measure);

uint8_t calculate_pid(struct pid_ctx *pid, int measure);

#endif 

#include "pid.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

void clear_memory(struct pid_ctx *pid, int temp){
  pid->err_count = 0;
  pid->pwm_count = 0;

  for(int i=0; i<PWM_MEM_SIZE; i++){
    pid->last_pwms[i] = 0;
  }

  for(int i=0; i<ERR_MEM_SIZE; i++){
    pid->err_mem[i] = 0;
  }

  pid->err_mem[pid->err_count] = temp;
}

struct pid_ctx *init_pid(unsigned p_const,unsigned i_const,unsigned d_const, int temp){
  struct pid_ctx *pid = (struct pid_ctx *)malloc(sizeof(struct pid_ctx));
  if(!pid){
    printf("Failed to alocate pid struct\n");
    exit(1);
  }

  clear_memory(pid, temp);

  pid->p_const   = p_const;
  pid->i_const   = i_const;
  pid->d_const   = d_const;

  pid->des_value = temp;
  pid->is_open = false;
  pid->safeguard_enabled = true;

  return pid;
}

uint8_t calculate_pwm(struct pid_ctx *pid, int measure){
  int pwm_val;

  int err = pid->des_value - measure;
  int err_sum = 0;


  for(int i=0; i<ERR_MEM_SIZE; i++){
    err_sum += pid->err_mem[i];
  }

  int prop  = 10*err/pid->p_const; // scaled values so we get more "resolution"
  int integ = err_sum/pid->i_const; 
  int deriv = 10*((int)pid->err_mem[(pid->err_count)%ERR_MEM_SIZE] - err)/(int)pid->d_const; //same

  pid->err_mem[pid->err_count + 1] = err;
  pid->err_count = (pid->err_count + 1) % ERR_MEM_SIZE;

  pwm_val = prop + integ + deriv; 
  uint8_t pwm;

  if(pwm_val > MAX_PWM){
    pwm = MAX_PWM;
  }else if(pwm_val < 0){
    pwm = 0;
  }else{
    pwm = (uint8_t) pwm_val;
  }

  pid->des_pwm = pwm;

  pid->pwm_count = (pid->pwm_count + 1)%PWM_MEM_SIZE;
  pid->last_pwms[pid->pwm_count] =  pwm;

  return pwm;
}

uint8_t safeguard(struct pid_ctx *pid, uint8_t des_pwm, int measure){
  if(des_pwm - pid->last_pwms[(pid->pwm_count + 1)%PWM_MEM_SIZE] >= 5){
    pid->last_pwms[pid->pwm_count] = pid->last_pwms[(pid->pwm_count + 1)%PWM_MEM_SIZE] + 5;
    return pid->last_pwms[pid->pwm_count];
  }
  
  if(des_pwm - pid->last_pwms[(pid->pwm_count + 1)%PWM_MEM_SIZE] <= -5){
    pid->last_pwms[pid->pwm_count] = pid->last_pwms[(pid->pwm_count + 1)%PWM_MEM_SIZE] - 5;
    return pid->last_pwms[pid->pwm_count];
  }

  if(pid->des_value - measure <= -20){
    pid->last_pwms[pid->pwm_count] *= 0.5f;
    return pid->last_pwms[pid->pwm_count];
  }

  return des_pwm;
}

uint8_t calculate_pid(struct pid_ctx *pid, int measure){
  uint8_t new_pwm = calculate_pwm(pid, measure);

  if(pid->safeguard_enabled)
    new_pwm = safeguard(pid, new_pwm, measure);

  return new_pwm;
}

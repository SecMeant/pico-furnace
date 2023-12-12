#include "pid.h"
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

struct pid_ctx *init_pid(unsigned p_const,unsigned i_const,unsigned d_const, int temp){
  struct pid_ctx *pid = (struct pid_ctx *)malloc(sizeof(struct pid_ctx));
  if(!pid){
    printf("Failed to alocate pid struct\n");
    exit(1);
  }

  pid->err_count = 0;
  pid->pwm_count = 0;

  for(int i=0; i<PWM_MEM_SIZE; i++){
    pid->last_pwms[i] = 0;
  }

  for(int i=0; i<ERR_MEM_SIZE; i++){
    pid->err_mem[i] = 0;
  }
  pid->err_mem[pid->err_count] = temp;

  pid->p_const   = p_const;
  pid->i_const   = i_const;
  pid->d_const   = d_const;

  pid->des_value = temp;
  pid->in_falling_mode = false;

  return pid;
}

uint8_t calculate_pwm(struct pid_ctx *pid, int measure){
  int pwm_val;

  int err = pid->des_value - measure;
  if(pid->in_falling_mode){
    pid->pwm_count = (pid->pwm_count +1)%PWM_MEM_SIZE;
    int pwm_ = pid->last_pwms[(pid->pwm_count - 3)%PWM_MEM_SIZE] - 1;
    if(pwm_ > MAX_PWM){
      pwm_ = MAX_PWM;
    }
    if(pwm_ < 0){
      pwm_ = 0;
    }
    pid->last_pwms[pid->pwm_count] =  (uint8_t)pwm_;
    return (uint8_t) pwm_;
  }

  if(err <= -50){
    pid->in_falling_mode = true;
  }

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
  if(pwm - pid->last_pwms[pid->pwm_count] >= 5){
    pwm = pid->last_pwms[pid->pwm_count] + 5;
    pid->last_pwms[pid->pwm_count] += 5;
    return pwm;
  }

  pid->last_pwms[pid->pwm_count] =  pwm;

  return pwm;
}

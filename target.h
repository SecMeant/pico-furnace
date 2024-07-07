#pragma once

#if CONFIG_SHUTTER
  #include "shutter.h"
#endif


typedef struct {
  struct tcp_pcb* server_pcb;
  struct tcp_pcb* client_pcb;
  uint8_t         recv_buffer[BUF_SIZE];
  uint16_t        recv_len; /* Received, valid bytes in recv_buffer */
} tcp_context_t;

#if CONFIG_AUTO == CONFIG_AUTO_PILOT || CONFIG_AUTO == CONFIG_AUTO_MAPPER
typedef struct {
  absolute_time_t pilot_deadline;
  bool            is_enabled;
  int             des_temp;
  int             last_temp;
} pilot_context_t;
#endif

#if CONFIG_AUTO == CONFIG_AUTO_MAPPER
typedef struct {
  absolute_time_t deadline;
  bool            is_enabled;
  int             max_pwm_temp;
} mapper_context_t;

  #define PWM_MAPPER_MINUTES 1
#endif

typedef struct {
  uint8_t  buffer[BUF_SIZE];
  uint8_t* parser;
} stdio_context_t;

typedef struct {
  absolute_time_t update_deadline;
  int             cur_temp;
  tcp_context_t   tcp;
  stdio_context_t stdio;
  uint8_t         log_bits;

#if CONFIG_AUTO == CONFIG_AUTO_PILOT || CONFIG_AUTO == CONFIG_AUTO_MAPPER
  pilot_context_t       pilot;
#endif
  uint8_t pwm_level;

  /*
   * Similar to MAX_PWM, but can be lowered at runtime to
   * make pwm never reach certain levels.
   *
   * This always holds true:
   *     pwm_level <= ceiling_pwm <= MAX_PWM
   */
  int             ceiling_pwm;

#if CONFIG_MAGNETRON
  uint8_t         pulse_count;
  absolute_time_t magnetron_deadline;
#endif

#if CONFIG_WATER
  /*
   *    pwm_water value is stored using already biased value
   *    which should be in range <WATER_OFFSET:MAX_PWM>
   *    any value outside that range is a bug.
   */
  uint8_t pwm_water;
#endif

#if CONFIG_SHUTTER
  shutter_context_t shutter;
#endif
#if CONFIG_AUTO == CONFIG_AUTO_MAPPER
  mapper_context_t  mapper;
#endif
} furnace_context_t;



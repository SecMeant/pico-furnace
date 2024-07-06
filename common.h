#pragma once

#define CONSTEVAL_HEADER "consteval_header.h"

#define MAX_TEMP 1100
#define MAX_PWM ((unsigned int)(CONFIG_MAX_PWM))
#define MAX_AUTO 1

#define FORMAT_STATUS_FMT "temp:%d/%d, pwm:%u/%u/%u, auto:%d\n"

#if CONFIG_AUTO == CONFIG_AUTO_MAPPER
/*
 * We are adding '!!!' at the beginning and at the end, so
 * it is easier to search for max temp on given pwm in logs.
 */

  #define MAPPER_STATUS_FMT     "!!! pwm:%u, max_temp:%d !!!\n"

  #define FALLBACK_TEMP         MAX_TEMP - 400
#endif

#if CONFIG_AUTO == CONFIG_AUTO_NONE
  #define FORMAT_STATUS_AUTO_NONE "temp:%d, pwm:%u/%u\n"
#endif

#if CONFIG_STIRRER
  #define STIRRER_PIN 27
#endif

#define STR(X) STR_HELPER(X)
#define STR_HELPER(X) #X

#define BUF_SIZE 64

#define FLASH_WRITE_MS 5000

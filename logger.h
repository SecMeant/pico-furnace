#pragma once

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#define SERVER       "server"
#define THERMOCOUPLE "thermocouple"

#define \
LOG_MSG_BUFFER_SIZE (sizeof(SERVER) + sizeof(THERMOCOUPLE) \
                    + sizeof("\r\n"))

enum
log_kind {
  LOG_KIND_SERVER       = 1 << 0,
  LOG_KIND_THERMOCOUPLE = 1 << 1
};

typedef struct {
  uint8_t log_value;
  size_t  length;
  char*   log_name;
} logger_pair_t;

void
set_log(const char*, const unsigned, uint8_t*);

size_t
get_logs(char*, const uint8_t);

static void
log_stdout(const uint8_t log_level, enum log_kind kind, const char* fmt, ...) {
  bool set = log_level & (uint8_t) kind;
  if (set == 0)
    return;

  va_list args;
  va_start(args, fmt);

  vprintf(fmt, args);

  va_end(args);
}

#define log_stdout_server(log, fmt, ...)       log_stdout(log, LOG_KIND_SERVER, fmt, ##__VA_ARGS__)
#define log_stdout_thermocouple(log, fmt, ...) log_stdout(log, LOG_KIND_THERMOCOUPLE, fmt, ##__VA_ARGS__)

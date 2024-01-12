#include "logger.h"

#define BUILD_BUG_ON_ZERO(expr) ((int)(sizeof(struct { int:(-!!(expr)); })))
#define __same_type(a, b) __builtin_types_compatible_p(typeof(a), typeof(b))
#define ARRAY_SIZE(arr) \
        (BUILD_BUG_ON_ZERO(__same_type((arr), &(arr)[0])), (sizeof(arr) / sizeof((arr)[0])))
#define LOGGER_OPTIONS_SIZE ARRAY_SIZE(logger)

static const logger_pair_t logger[] = {
  {
    .log_name  = SERVER,
    .log_value = LOG_KIND_SERVER,
    .length    = sizeof(SERVER)
  },
  {
    .log_name  = THERMOCOUPLE,
    .log_value = LOG_KIND_THERMOCOUPLE,
    .length    = sizeof(THERMOCOUPLE)
  }
};

static const logger_pair_t*
find_logger_by_name(const char* const name)
{
  const logger_pair_t* begin = &logger[0];
  const logger_pair_t* const end = begin + LOGGER_OPTIONS_SIZE;

  while (begin != end && strcmp(name, begin->log_name))
   ++begin;

  return begin;
}

void
set_log(const char* buf, const unsigned set, uint8_t* log_bits)
{
  const logger_pair_t* const log     = find_logger_by_name(buf);
  const logger_pair_t* const log_end = &logger[0] + LOGGER_OPTIONS_SIZE;

  if(log == log_end)
    return;

  int offset = log - &logger[0];

  if(set == 1)
    *log_bits |=  (0x1 << offset);
  else
    *log_bits &= ~(0x1 << offset);
}

size_t
get_logs(char* msg, const uint8_t log_bits)
{
  size_t msg_len = 0;

  for(int i = 0; i < LOGGER_OPTIONS_SIZE; i++)
  {
    const bool set = log_bits & (0x1 << i);
    if(set != 1)
      continue;

    msg_len += snprintf(msg + msg_len, logger[i].length, logger[i].log_name);

    // change '\0' to ' ' in case of another log name
    msg[msg_len++] = ' ';
  }

  if(msg_len == 0)
    return 0;

  msg[msg_len - 1] = '\r';
  msg[msg_len++]   = '\n';
  msg[msg_len++]   = '\0';

  return msg_len;
}

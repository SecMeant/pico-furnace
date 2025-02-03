#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "pico/bootrom.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "hardware/spi.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#include "spi_config.h"
#if CONFIG_THERMO
  #include "max318xx.h"
#endif
#include "logger.h"

#if CONFIG_FLASH
  #include "flash_io.h"
#endif

#include "common.h"
#include CONSTEVAL_HEADER

#define TCP_PORT        4242
#define DEBUG_printf    printf


/* GPIO for enabling and disabling heating of the furnace. */
#define FURNACE_FIRE_PIN CONFIG_FURNACE_FIRE_PIN
#define FURNACE_FIRE_PWM_SLICE pwm_gpio_to_slice_num(FURNACE_FIRE_PIN)
#define FURNACE_FIRE_PWM_CHANNEL pwm_gpio_to_channel(FURNACE_FIRE_PIN)

#if CONFIG_SHUTTER
  #include "shutter.c"
#endif

#include "target.h"

#if CONFIG_WATER
  #define WATER_PIN 9
  #define WATER_PIN_SLICE pwm_gpio_to_slice_num(WATER_PIN)
#endif

#include "pwm.c"

#if CONFIG_MAGNETRON
  #include "magnetron.c"
#endif

int
max318xx_init(void);

static err_t
tcp_server_close(furnace_context_t* ctx)
{
  err_t err = ERR_OK;

  if (ctx->tcp.client_pcb != NULL) {
    tcp_arg(ctx->tcp.client_pcb, NULL);
    tcp_recv(ctx->tcp.client_pcb, NULL);
    tcp_err(ctx->tcp.client_pcb, NULL);
    err = tcp_close(ctx->tcp.client_pcb);

    if (err != ERR_OK) {
      log_stdout_server(ctx->log_bits, "close failed %d, calling abort\n", err);
      tcp_abort(ctx->tcp.client_pcb);
      err = ERR_ABRT;
    }

    ctx->tcp.client_pcb = NULL;
  }

  if (ctx->tcp.server_pcb) {
    tcp_arg(ctx->tcp.server_pcb, NULL);
    tcp_close(ctx->tcp.server_pcb);
    ctx->tcp.server_pcb = NULL;
  }

  return err;
}

static err_t
tcp_server_send_data(furnace_context_t* ctx,
                     struct tcp_pcb*    tpcb,
                     const uint8_t*     data,
                     u16_t              size)
{
  return tcp_write(tpcb, data, size, TCP_WRITE_FLAG_COPY);
}

static void
tcp_server_recv_(furnace_context_t *ctx, struct tcp_pcb* tpcb, struct pbuf* p)
{
  log_stdout_server(ctx->log_bits, "tcp_server_recv %d\n", p->tot_len);

  if (p->tot_len == 0)
    return;

  /*
   * Receive the buffer
   *
   * TODO: We should probably call pbuf_copy_partial in a loop
   *       to make sure we receive everything.
   */
  const uint16_t buffer_left = BUF_SIZE;
  ctx->tcp.recv_len =
    pbuf_copy_partial(p, ctx->tcp.recv_buffer, p->tot_len > buffer_left ? buffer_left : p->tot_len, 0);
  tcp_recved(tpcb, p->tot_len);
}

#if CONFIG_WATER
static void
handle_command_water(furnace_context_t* ctx, void (*feedback)(const char *, const size_t), unsigned arg) {
    const int res = set_pwm_safe(WATER_PIN, ctx, arg);

    if(res == 0)
      return;

    if (res  == 1){
      const char msg[] = "water pwm argument too big!\r\n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
      return;
    }

    if(res == -1){
      const char msg[] = "set_pwm_safe: unexpected pin argument!\r\n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
      return;
    }

    const char msg[] = "set_pwm_safe: unexpected return value!\r\n";
    const size_t msg_len = sizeof(msg)-1;
    feedback(msg, msg_len);
}
#endif

static int
set_max_pwm_safe(furnace_context_t *ctx, int new_max_pwm)
{
    if (new_max_pwm > MAX_PWM)
      return 1;

    if (ctx->pwm_level > new_max_pwm)
      ctx->pwm_level = new_max_pwm;

    ctx->ceiling_pwm = new_max_pwm;

    return 0;
}

#if CONFIG_STIRRER
static void
set_stirrer(bool opt)
{
  gpio_put(STIRRER_PIN, opt);
}
#endif

static void
command_handler(furnace_context_t* ctx, uint8_t* buffer, void (*feedback)(const char*, const size_t))
{
  unsigned arg;
  char     str_arg[BUF_SIZE];

  if (buffer[0] == '\n') return;

  if (memcmp(buffer, "reboot", 6) == 0) {
    reset_usb_boot(0,0);
  } else if (sscanf(buffer, "max_pwm %u", &arg) == 1) {
    const int res = set_max_pwm_safe(ctx, arg);
    if (res == 1) {
      const char msg[] = "pwm argument too big!\r\n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
    }
  } else if (strncmp(buffer, "max_pwm\n", 8) == 0) {
      char msg[MAX_PWM_FMT_SIZE];
      const size_t msg_len = snprintf(msg, sizeof(msg), MAX_PWM_STATUS_FMT, ctx->ceiling_pwm);
      feedback(msg, msg_len);
  } else if (strncmp(buffer, "pwm\n", 4) == 0) {
      char msg[16];
      const size_t msg_len = snprintf(msg, sizeof(msg), "pwm = %d\r\n", ctx->pwm_level);
      feedback(msg, msg_len);
  } else if (sscanf(buffer, "pwm %u", &arg) == 1) {
    const int res= set_pwm_safe(FURNACE_FIRE_PIN, ctx, arg);
    if (res == 0){
#if CONFIG_AUTO == CONFIG_AUTO_PILOT || CONFIG_AUTO == CONFIG_AUTO_MAPPER
      ctx->pilot.is_enabled = 0;
#endif
    } else if ( res == 1){
      const char msg[] = "pwm argument too big!\r\n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
    } else if ( res == -1){
      const char msg[] = "set_pwm_safe: unexpected pin argument!\r\n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
    } else {
      const char msg[] = "set_pwm_safe: unexpected return value!\r\n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
    }
  }
#if CONFIG_AUTO == CONFIG_AUTO_PILOT
  else if (strncmp(buffer, "auto\n", 5) == 0) {
      char msg[16];
      const size_t msg_len = snprintf(msg, sizeof(msg), "auto = %d\r\n", ctx->pilot.is_enabled);
      feedback(msg, msg_len);
  } else if (sscanf(buffer, "auto %u", &arg) == 1) {
    if (arg > 1) {
      const char msg[] = "auto argument needs to be 0 or 1\r\n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
    } else {
      ctx->pilot.is_enabled = arg;
    }
  } else if (sscanf(buffer, "temp %u", &arg) == 1) {
    if (arg > MAX_TEMP) {
      const char msg[] = "temp argument too big!\r\n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
    } else {
      ctx->pilot.des_temp = arg;
    }
  } else if (strncmp(buffer, "temp\n", 5) == 0) {
    char msg[16];
    const size_t msg_len = snprintf(msg, sizeof(msg), "temp = %d\r\n", ctx->pilot.des_temp);
    feedback(msg, msg_len);
  }
#endif
  else if (sscanf(buffer, "log %" STR(BUF_SIZE) "s %u", &str_arg, &arg) == 2) {
    if(arg >= 2) {
      const char msg[] = "log value too big!\r\n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
    } else {
      set_log(str_arg, arg, &ctx->log_bits);
    }
  } else if (strncmp(buffer, "log\n", 4) == 0) {
    char msg[LOG_MSG_BUFFER_SIZE];
    const size_t msg_len = get_logs(msg, ctx->log_bits);
    feedback(msg, msg_len);
  } else if(memcmp(buffer, "help\n", 5) == 0) {

    const char msg[] = "help              \t\t shows this message\n"
                        "reboot            \t\t reboot device\n"
                        "pwm <0;50>        \t\t sets pwm\n"
                        "pwm               \t\t prints current pwm level\n"
                        "max_pwm <0;50>    \t\t sets max pwm level.\n"
                        "                  \t\t\t Device will never exceed this pwm value\n"
#if CONFIG_THERMO && CONFIG_AUTO == CONFIG_AUTO_PILOT
                        "temp <0;" STR(MAX_TEMP) ">     \t\t sets wanted temperature\n"
                        "temp              \t\t shows current wanted temperature\n"
                        "auto <0;1>        \t\t sets automatic pwm control, it is\n"
                        "                  \t\t reaching temperature set by 'temp' command\n"
                        "                  \t\t\t 0 - off\n"
                        "                  \t\t\t 1 - on\n"
                        "auto              \t\t shows current auto status\n"
#endif
#if CONFIG_MAGNETRON
                        "pulse <0:127>     \t\t starts pulses of magnetron\n"
#endif
#if CONFIG_WATER
                        "water <0:10>      \t\t sets pwm duty of water channel\n"
                        "water             \t\t shows current water pwm\n"
#endif
#if CONFIG_AUTO == CONFIG_AUTO_MAPPER
                        "map <0;1>         \t\t sets automatic pwm mapping, it is\n"
                        "                  \t\t checking max temperature on every pwm\n"
                        "                  \t\t\t 0 - off\n"
                        "                  \t\t\t 1 - on\n"
                        "map               \t\t shows current map status\n"
#endif
#if CONFIG_STIRRER
                        "stir <0;1>        \t\t turns on the stirring cap for beaker\n"
                        "                  \t\t\t 0 - off\n"
                        "                  \t\t\t 1 - on\n"
#endif
                        "log <option> <0;1>\t\t sets output level on stdio\n"
                        "                  \t\t\t options:\n"
                        "                  \t\t\t\t server,\n"
                        "                  \t\t\t\t thermocouple,\n"
                        "                  \t\t\t\t basic\n"
                        "                  \t\t\t 0 - off\n"
                        "                  \t\t\t 1 - on\n"
                        "log               \t\t prints names of turned on log options\n";
    const size_t msg_len = sizeof(msg)-1;
    feedback(msg, msg_len);
  }
#if CONFIG_MAGNETRON
  else if(sscanf(buffer, "pulse %u", &arg) == 1) {
    if(arg > 127){
      const char msg[] = "pulse value too big!\r\n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
    } else {
      ctx->pulse_count = arg*2;
    }
  }
#endif
#if CONFIG_WATER
  else if(sscanf(buffer, "water %u", &arg) == 1) {
    handle_command_water(ctx, feedback, arg);
  } else if(memcmp(buffer, "water\n", 6) == 0){
    char msg[16];
    const size_t msg_len = snprintf(msg, sizeof(msg), "water = %d\r\n", ctx->pwm_water);
    feedback(msg, msg_len);
  }
#endif
#if CONFIG_SHUTTER
  else if(sscanf(buffer, "shutter %u", &arg) == 1){
    if(arg > MAX_SHUTTER_MS){
      const char msg[] = "time argument too big !!!\r\n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
    } else if(ctx->shutter.time_ms == 0){
      ctx->shutter.time_ms = arg;
    } else{
      const char msg[] = "shutter already in work \n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
    }
  } else if(sscanf(buffer, "shutter %s", str_arg) == 1) {
    if(strncmp(str_arg, "on", 2) == 0){
      ctx->shutter.time_ms = 1;
      ctx->shutter.intern_state = SHUTTER_ON_OPTION;
    }else if((strncmp(str_arg, "off", 3) == 0)){
      ctx->shutter.time_ms = 1;
      ctx->shutter.intern_state = SHUTTER_OFF_OPTION;
    }
  }
#endif
#if CONFIG_AUTO == CONFIG_AUTO_MAPPER
  else if(sscanf(buffer, "map %u", &arg) == 1) {
    if(arg > 1){
      const char msg[] = "map argument needs to be 0 or 1\r\n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
    } else {
      if(ctx->cur_temp >= 40){
        const char msg[] = "map can be started only at temperatures lower than 40\r\n";
        const size_t msg_len = sizeof(msg)-1;
        feedback(msg, msg_len);
      } else {
        ctx->pwm_level = 0;
        ctx->pilot.is_enabled = false;
        ctx->mapper.is_enabled = arg;
      }
    }
  } else if(strncmp(buffer, "map\n", 4) == 0) {
      char msg[16];
      const size_t msg_len = snprintf(msg, sizeof(msg), "map = %d\r\n", ctx->mapper.is_enabled);
      feedback(msg, msg_len);
  }
#endif
#if CONFIG_STIRRER
  else if(sscanf(buffer, "stir %u", &arg) == 1){
    if(arg > 1){
      const char msg[] = "stir argument needs to be 0 or 1\r\n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
    } else {
      set_stirrer((bool) arg);
    }
  }
#endif
}

static void
tcp_command_handler(furnace_context_t* ctx, struct tcp_pcb* tpcb)
{
  void
  send(const char* msg, const size_t msg_len)
  {
    tcp_server_send_data(ctx, tpcb, msg, msg_len);
  }

  command_handler(ctx, ctx->tcp.recv_buffer, &send);
}

static err_t
tcp_server_recv(void* ctx_, struct tcp_pcb* tpcb, struct pbuf* p, err_t err)
{
  furnace_context_t *ctx = (furnace_context_t*)ctx_;

  if (!p) {
    tcp_server_close(ctx);
    return err;
  }

  tcp_server_recv_(ctx, tpcb, p);
  //We are expecting only strings, we can manually terminate the string
  ctx->tcp.recv_buffer[p->tot_len] = '\0';

  pbuf_free(p);

  // Echo back for debugging
  // tcp_server_send_data(ctx, tpcb, ctx->recv_buffer, ctx->recv_len);

  tcp_command_handler(ctx, tpcb);

  log_stdout_server(ctx->log_bits, "tcp_server_recv: %.*s\n", p->tot_len, ctx->tcp.recv_buffer);

  return ERR_OK;
}

static void
tcp_server_err(void* ctx_, err_t err)
{
  furnace_context_t *ctx = (furnace_context_t*)ctx_;

  log_stdout_server(ctx->log_bits, "tcp_client_err_fn %d\n", err);
  tcp_server_close(ctx);
}

static err_t
tcp_server_accept(void* ctx_, struct tcp_pcb* client_pcb, err_t err)
{
  furnace_context_t *ctx = (furnace_context_t*)ctx_;

  if (err != ERR_OK || client_pcb == NULL) {
    log_stdout_server(ctx->log_bits, "Failure in accept\n");
    tcp_server_close(ctx);
    return ERR_VAL;
  }

  log_stdout_server(ctx->log_bits, "Client connected\n");

  ctx->tcp.client_pcb = client_pcb;
  tcp_arg(client_pcb, ctx);
  tcp_recv(client_pcb, tcp_server_recv);
  tcp_err(client_pcb, tcp_server_err);

  client_pcb->so_options |= SOF_KEEPALIVE;
  client_pcb->keep_intvl = 4000; /* 4 seconds */

  return ERR_OK;
}

static bool
tcp_server_open(void* ctx_)
{
  furnace_context_t *ctx = (furnace_context_t*)ctx_;

  log_stdout_server(ctx->log_bits,
                     "Starting server at %s on port %u\n",
                     ip4addr_ntoa(netif_ip4_addr(netif_list)),
                     TCP_PORT);

  struct tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  if (!pcb) {
    log_stdout_server(ctx->log_bits, "Failed to create pcb\n");
    return false;
  }

  err_t err = tcp_bind(pcb, NULL, TCP_PORT);
  if (err) {
    log_stdout_server(ctx->log_bits, "Failed to bind to port %u\n", TCP_PORT);
    return false;
  }

  ctx->tcp.server_pcb = tcp_listen_with_backlog(pcb, 1);
  if (!ctx->tcp.server_pcb) {
    log_stdout_server(ctx->log_bits, "Failed to listen\n");
    if (pcb) {
      tcp_close(pcb);
    }
    return false;
  }

  tcp_arg(ctx->tcp.server_pcb, ctx);
  tcp_accept(ctx->tcp.server_pcb, tcp_server_accept);

  return true;
}

static void
do_thermocouple_work(furnace_context_t *ctx, bool deadline_met)
{
  if (!deadline_met)
    return;

#if CONFIG_THERMO
  ctx->cur_temp = max318xx_read_temperature();
#endif
#if CONFIG_THERMO == CONFIG_THERMO_KTYPE
  log_stdout_thermocouple(ctx->log_bits, "cold: %u\n", max318xx_read_cold_junction());
#endif
  log_stdout_thermocouple(ctx->log_bits, "hot: %u\n", ctx->cur_temp);
}

static int
format_status(char* buffer, furnace_context_t* ctx)
{
#if CONFIG_AUTO == CONFIG_AUTO_NONE
  return snprintf(
      buffer,
      FORMAT_STATUS_AUTO_NONE_SIZE,
      FORMAT_STATUS_AUTO_NONE,
      ctx->cur_temp,
      ctx->pwm_level,
      MAX_PWM
      );
#else
    return snprintf(
      buffer,
      FORMAT_STATUS_AUTO_PILOT_SIZE,
      FORMAT_STATUS_FMT,
      ctx->cur_temp,
      ctx->pilot.des_temp,
      ctx->pwm_level,
      ctx->ceiling_pwm,
      MAX_PWM,
      ctx->pilot.is_enabled
    );
#endif
}

#if CONFIG_AUTO == CONFIG_AUTO_MAPPER

static int
format_mapper(char *buffer, furnace_context_t *ctx)
{
    return snprintf(
      buffer,
      MAPPER_STATUS_SIZE,
      MAPPER_STATUS_FMT,
      ctx->pwm_level,
      ctx->mapper.max_pwm_temp
    );
}

#endif

static void
do_tcp_work(furnace_context_t *ctx, bool deadline_met)
{
#if CONFIG_AUTO == CONFIG_AUTO_NONE
  char temperature_str[FORMAT_STATUS_AUTO_NONE_SIZE];
#else
  char temperature_str[FORMAT_STATUS_AUTO_PILOT_SIZE];
#endif

  cyw43_arch_poll();

  // If disconnected, reset and setup listening
  if (ctx->tcp.server_pcb == NULL || ctx->tcp.server_pcb->state == CLOSED) {
    memset(&ctx->tcp, 0, sizeof(ctx->tcp));
    if (!tcp_server_open(ctx)) {
      tcp_server_close(ctx);
    }
  }

  if (ctx->tcp.client_pcb && deadline_met) {
    const int temperature_str_len = format_status(temperature_str, ctx);

    tcp_server_send_data(
      ctx,
      ctx->tcp.client_pcb,
      (uint8_t*)temperature_str,
      temperature_str_len
    );
  }

}

static inline int
sgn(int val_1, int val_2)
{
  if(val_1 > val_2)
    return -1;
  return 1;
}

static inline uint8_t
clamp_u8(int min, int max, int val)
{
  uint8_t value = (uint8_t)val;

  if(val < min)
    value = min;

  if(val > max)
    value = max;

  return value;
}

static void
init_furnace(furnace_context_t *ctx)
{
  memset(ctx, 0, sizeof(*ctx));

  ctx->ceiling_pwm = MAX_PWM;
}

#if CONFIG_AUTO == CONFIG_AUTO_PILOT || CONFIG_AUTO == CONFIG_AUTO_MAPPER
static void
do_pilot_work(furnace_context_t *ctx)
{
  if (!ctx->pilot.is_enabled)
    return;

  const bool deadline_met = get_absolute_time() > ctx->pilot.pilot_deadline;

  if (!deadline_met)
    return;

  const int min_increase_rate = 1;
  const int diff = ctx->cur_temp - ctx->pilot.last_temp;
  const int sign = sgn(ctx->cur_temp, ctx->pilot.des_temp);
  unsigned pwm = ctx->pwm_level;

  ctx->pilot.last_temp = ctx->cur_temp;
  ctx->pilot.pilot_deadline = make_timeout_time_ms(CONFIG_FURNACE_DEADLINE_MS);

  /*
   * If going in the wrong direction
   * or going in the good direction,
   * but slowly - nudge the PWM.
   */
  if(sign*diff <= min_increase_rate)
    pwm += sign;

  set_pwm_safe(FURNACE_FIRE_PIN, ctx, pwm);
}

static void
init_pilot(furnace_context_t *ctx)
{
  ctx->pilot.des_temp = 0;
  ctx->pilot.last_temp = 0;
  ctx->pilot.is_enabled = false;
}
#endif

#if CONFIG_STIRRER
static void
init_stirrer(void)
{
  gpio_init(STIRRER_PIN);
  gpio_set_dir(STIRRER_PIN, GPIO_OUT);
}
#endif

static void
init_stdio(furnace_context_t* ctx)
{
  ctx->stdio.parser = ctx->stdio.buffer;
  memset(&ctx->stdio.buffer, 0, BUF_SIZE);
}

static void
reset_stdio_data(furnace_context_t* ctx)
{
  memset(&ctx->stdio.buffer, 0, BUF_SIZE);
  ctx->stdio.parser = ctx->stdio.buffer;
}

static void
stdio_command_handler(furnace_context_t* ctx)
{
  void
  send_stdio(const char* msg, const size_t msg_len)
  {
    if(msg_len == 0) return;
    printf(msg);
  }

  command_handler(ctx, ctx->stdio.buffer, &send_stdio);
}

static void
do_stdio_work(furnace_context_t* ctx, bool deadline_met)
{
  if(deadline_met)
  {
    char buffer[FORMAT_STATUS_AUTO_PILOT_SIZE];

    format_status(buffer, ctx);
    log_stdout_basic(ctx->log_bits, buffer);
  }

  while(1) {
    uint8_t c = getchar_timeout_us(0);

    if(c == (uint8_t) PICO_ERROR_TIMEOUT) return;

    if(ctx->stdio.parser == ctx->stdio.buffer + BUF_SIZE){
      printf("\nLines longer than %d are invalid!\nResetting stdio buffer.\n", BUF_SIZE);
      reset_stdio_data(ctx);
      return;
    }

    // User may change last sent character from lf to cr, vice versa or crlf
    // Although, in parsing command, we do not care how user sent this
    // and we are always putting '\n' at the end of command
    if(c == '\n' || c == '\r') {
      *ctx->stdio.parser = '\n';
      stdio_command_handler(ctx);
      reset_stdio_data(ctx);
      return;
    }

    *ctx->stdio.parser = c;
    ctx->stdio.parser++;
  }
}

#if CONFIG_AUTO == CONFIG_AUTO_MAPPER

static void
mapper_deadline__(furnace_context_t *ctx)
{
  if(ctx->cur_temp > ctx->mapper.max_pwm_temp){
    ctx->mapper.max_pwm_temp = ctx->cur_temp;
    return;
  }

  char buffer[MAPPER_STATUS_SIZE];

  const int size = format_mapper(buffer, ctx);
  tcp_server_send_data(
    ctx,
    ctx->tcp.client_pcb,
    (uint8_t*)buffer,
    size
  );

  const unsigned pwm = ctx->pwm_level + 1;

  const int res = set_pwm_safe(FURNACE_FIRE_PIN, ctx, pwm);

  if( res == -1 ) {
    const char msg[] = "set_pwm_safe: unexpected pin argument!\r\n";
    const size_t msg_len = sizeof(msg)-1;
    tcp_server_send_data(
      ctx,
      ctx->tcp.client_pcb,
      (uint8_t*)buffer,
      size
    );
  } else if ( res == 1 ) {
    const char msg[] = "pwm_level has reached MAX_PWM, enabling auto and steering temp towards FALLBACK_TEMP!\r\n";
    const size_t msg_len = sizeof(msg)-1;
    tcp_server_send_data(
      ctx,
      ctx->tcp.client_pcb,
      (uint8_t*)buffer,
      size
    );

    ctx->mapper.is_enabled = false;
    ctx->pilot.is_enabled = true;
    ctx->pilot.des_temp = FALLBACK_TEMP;
  }
}

static void
mapper_deadline_(furnace_context_t *ctx)
{
  mapper_deadline__(ctx);
  ctx->mapper.deadline = make_timeout_time_ms(PWM_MAPPER_MINUTES * 60 * 1000);
}

static void
mapper_maxtemp_reached_(furnace_context_t *ctx)
{
  const char msg[] = "cur_temp has reached MAX_TEMP, enabling auto and steering temp towards FALLBACK_TEMP!\r\n";
  const size_t msg_len = sizeof(msg)-1;
  tcp_server_send_data(
    ctx,
    ctx->tcp.client_pcb,
    (uint8_t*)msg,
    msg_len
  );

  ctx->mapper.is_enabled = false;
  ctx->pilot.is_enabled = true;
  ctx->pilot.des_temp = FALLBACK_TEMP;
}

static void
do_mapper_work(furnace_context_t *ctx)
{
  if(!ctx->mapper.is_enabled)
    return;

  if(ctx->cur_temp >= MAX_TEMP)
    return mapper_maxtemp_reached_(ctx);

  const bool deadline_met = get_absolute_time() > ctx->mapper.deadline;
  if (deadline_met)
    return mapper_deadline_(ctx);
}

#endif

static int
main_work_loop(void)
{
  furnace_context_t* ctx;

  ctx = malloc(sizeof(*ctx));

  if (!ctx) {
    return 1;
  }

  init_pwm();
  init_furnace(ctx);
#if CONFIG_AUTO == CONFIG_AUTO_MAPPER || CONFIG_AUTO == CONFIG_AUTO_PILOT
  init_pilot(ctx);
#endif
  init_stdio(ctx);

#if CONFIG_MAGNETRON
  init_magnetron(ctx);
#endif

#if CONFIG_STIRRER
  init_stirrer();
#endif

#if CONFIG_FLASH
  init_flash(ctx);
#endif

  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

  while (1) {
    const absolute_time_t now = get_absolute_time();
    const bool deadline_met = now > ctx->update_deadline;

#if CONFIG_THERMO
    do_thermocouple_work(ctx, deadline_met);
#endif
    do_tcp_work(ctx, deadline_met);
    do_stdio_work(ctx, deadline_met);
#if CONFIG_AUTO == CONFIG_AUTO_PILOT || CONFIG_AUTO == CONFIG_AUTO_MAPPER
    do_pilot_work(ctx);
#endif
#if CONFIG_SHUTTER
    do_shutter_work(&ctx->shutter);
#endif
#if CONFIG_AUTO == CONFIG_AUTO_MAPPER
    do_mapper_work(ctx);
#endif

#if CONFIG_FLASH
    do_flash_work(ctx);
#endif

    if (deadline_met)
      ctx->update_deadline = make_timeout_time_ms(1000);
#if CONFIG_MAGNETRON
    const bool magnetron_deadline = now > ctx->magnetron_deadline;
    do_magnetron_work(ctx, magnetron_deadline);
#endif
  }

  free(ctx);

  return 0;
}

int spi_main(void);

static int
main_(void)
{
  DEBUG_printf("Connecting to Wi-Fi...\n");

  if (cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 5000)) {
    DEBUG_printf("failed to connect.\n");
    return 1;
  } else {
    DEBUG_printf("Connected.\n");
  }

#if CONFIG_THERMO
  const int max318xx_init_status = max318xx_init();
  if (max318xx_init_status) {
    DEBUG_printf("max318xx init failed with %d\n", max318xx_init_status);
    return max318xx_init_status;
  }
#endif

  const int ret = main_work_loop();

  cyw43_arch_deinit();

  return ret;
}

int
main(void)
{
  stdio_init_all();

  if (cyw43_arch_init()) {
    DEBUG_printf("failed to initialise\n");
    return 1;
  }

  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
  cyw43_arch_enable_sta_mode();

  while(1) {
    int ret = main_();

    if (ret)
      DEBUG_printf("main() failed with %d\n", ret);

    sleep_ms(3000);
  }
}

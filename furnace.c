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
#include "max31856.h"

#define TCP_PORT        4242
#define DEBUG_printf    printf
#define BUF_SIZE        64

/* GPIO for enabling and disabling heating of the furnace. */
#define FURNACE_FIRE_PIN 21
#define FURNACE_FIRE_PWM_SLICE pwm_gpio_to_slice_num(FURNACE_FIRE_PIN)
#define FURNACE_FIRE_PWM_CHANNEL pwm_gpio_to_channel(FURNACE_FIRE_PIN)

#define MAX_TEMP 1250

typedef struct {
  struct tcp_pcb* server_pcb;
  struct tcp_pcb* client_pcb;
  uint8_t         recv_buffer[BUF_SIZE];
  uint16_t        recv_len; /* Received, valid bytes in recv_buffer */
} tcp_context_t;

typedef struct {
  absolute_time_t pilot_deadline;
  bool            is_enabled;
  int             des_temp;
  int             last_temp;
} pilot_context_t;

typedef struct {
  uint8_t  buffer[BUF_SIZE];
  uint8_t* parser;
} stdio_context_t;

typedef struct {
  absolute_time_t update_deadline;
  int             cur_temp;
  tcp_context_t   tcp;
  stdio_context_t stdio;

  pilot_context_t       pilot;
  uint8_t pwm_level;
} furnace_context_t;

#include "pwm.c"

int
max31856_init(void);

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
      DEBUG_printf("close failed %d, calling abort\n", err);
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

err_t
tcp_server_send_data(furnace_context_t* ctx,
                     struct tcp_pcb*    tpcb,
                     const uint8_t*     data,
                     u16_t              size)
{
  return tcp_write(tpcb, data, size, TCP_WRITE_FLAG_COPY);
}

void
tcp_server_recv_(furnace_context_t *ctx, struct tcp_pcb* tpcb, struct pbuf* p)
{
  DEBUG_printf("tcp_server_recv %d\n", p->tot_len);

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

static void
command_handler(furnace_context_t* ctx, uint8_t* buffer, void (*feedback)(const char*, const size_t))
{
  unsigned arg;

  if (buffer[0] == '\n') return;

  if (memcmp(buffer, "reboot", 6) == 0) {
    reset_usb_boot(0,0);
  } else if (strncmp(buffer, "pwm\n", 4) == 0) {
      char msg[16];
      const size_t msg_len = snprintf(msg, sizeof(msg), "pwm = %d\r\n", ctx->pwm_level);
      feedback(msg, msg_len);
  } else if (sscanf(buffer, "pwm %u", &arg) == 1) {
    if (set_pwm_safe(ctx, arg) == 1){
      const char msg[] = "pwm argument too big!\r\n";
      const size_t msg_len = sizeof(msg)-1;
      feedback(msg, msg_len);
    }
  } else if (strncmp(buffer, "auto\n", 5) == 0) {
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
  } else if(memcmp(buffer, "help\n", 5) == 0) {
    const char msg[] = "help             shows this message\n"
                        "reboot           reboot device\n"
                        "pwm <0;50>       sets pwm\n"
                        "pwm              prints current pwm level\n"
                        "temp <0;1250>    sets wanted temperature\n"
                        "temp             shows current wanted temperature\n"
                        "auto <0;1>       sets automatic pwm control, it is\n"
                        "                 reaching temperature set by 'temp' command\n"
                        "                 0 - off\n"
                        "                 1 - on\n"
                        "auto             shows current auto status\n";
    const size_t msg_len = sizeof(msg)-1;
    feedback(msg, msg_len);
  }
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

  DEBUG_printf("tcp_server_recv: %.*s\n", p->tot_len, ctx->tcp.recv_buffer);

  return ERR_OK;
}

static void
tcp_server_err(void* ctx_, err_t err)
{
  furnace_context_t *ctx = (furnace_context_t*)ctx_;

  DEBUG_printf("tcp_client_err_fn %d\n", err);
  tcp_server_close(ctx);
}

static err_t
tcp_server_accept(void* ctx_, struct tcp_pcb* client_pcb, err_t err)
{
  furnace_context_t *ctx = (furnace_context_t*)ctx_;

  if (err != ERR_OK || client_pcb == NULL) {
    DEBUG_printf("Failure in accept\n");
    tcp_server_close(ctx);
    return ERR_VAL;
  }

  DEBUG_printf("Client connected\n");

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

  DEBUG_printf("Starting server at %s on port %u\n",
               ip4addr_ntoa(netif_ip4_addr(netif_list)),
               TCP_PORT);

  struct tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  if (!pcb) {
    DEBUG_printf("failed to create pcb\n");
    return false;
  }

  err_t err = tcp_bind(pcb, NULL, TCP_PORT);
  if (err) {
    DEBUG_printf("failed to bind to port %u\n", TCP_PORT);
    return false;
  }

  ctx->tcp.server_pcb = tcp_listen_with_backlog(pcb, 1);
  if (!ctx->tcp.server_pcb) {
    DEBUG_printf("failed to listen\n");
    if (pcb) {
      tcp_close(pcb);
    }
    return false;
  }

  tcp_arg(ctx->tcp.server_pcb, ctx);
  tcp_accept(ctx->tcp.server_pcb, tcp_server_accept);

  return true;
}

void
do_thermocouple_work(furnace_context_t *ctx, bool deadline_met)
{
  if (!deadline_met)
    return;

  const bool rdy = gpio_get(FURNACE_MAX31856_RDY);
  if (rdy)
    DEBUG_printf("RDY: %d\n", (int) rdy);

  ctx->cur_temp = max31856_read_temperature();
  DEBUG_printf("cold: %u\n", max31856_read_cold_junction());
  DEBUG_printf("hot: %u\n", ctx->cur_temp);
}

void
do_tcp_work(furnace_context_t *ctx, bool deadline_met)
{
  char temperature_str[32];

  cyw43_arch_poll();

  // If disconnected, reset and setup listening
  if (ctx->tcp.server_pcb == NULL || ctx->tcp.server_pcb->state == CLOSED) {
    memset(&ctx->tcp, 0, sizeof(ctx->tcp));
    if (!tcp_server_open(ctx)) {
      tcp_server_close(ctx);
    }
  }

  if (ctx->tcp.client_pcb && deadline_met) {
    const int temperature_str_len = snprintf(
      temperature_str,
      sizeof(temperature_str),
      "temp:%d, pwm:%u/%u\n",
      ctx->cur_temp,
      ctx->pwm_level,
      MAX_PWM
    );

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
do_pilot_work(furnace_context_t *ctx)
{
  const bool deadline_met = get_absolute_time() > ctx->pilot.pilot_deadline;

  if(deadline_met && ctx->pilot.is_enabled){
    const int diff = ctx->cur_temp - ctx->pilot.last_temp;
    const int sign = sgn(ctx->cur_temp, ctx->pilot.des_temp);
    unsigned pwm = ctx->pwm_level;

    ctx->pilot.last_temp = ctx->cur_temp;
    ctx->pilot.pilot_deadline = make_timeout_time_ms(15000);

    if(diff >= -5 && diff <= 1)
      pwm += sign;

    set_pwm_safe(ctx, pwm);
  }
}

void
init_pilot(furnace_context_t *ctx)
{
  ctx->pilot.des_temp = 0;
  ctx->pilot.last_temp = 0;
  ctx->pilot.is_enabled = false;
}

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
    printf(msg);
  }

  command_handler(ctx, ctx->stdio.buffer, &send_stdio);
}

void
do_stdio_work(furnace_context_t* ctx)
{
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

int
main_work_loop(void)
{
  furnace_context_t* ctx;

  ctx = calloc(1, sizeof(furnace_context_t));

  if (!ctx) {
    return 1;
  }

  init_pwm();
  init_pilot(ctx);
  init_stdio(ctx);

  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

  while (1) {
    const bool deadline_met = get_absolute_time() > ctx->update_deadline;

    do_thermocouple_work(ctx, deadline_met);
    do_tcp_work(ctx, deadline_met);
    do_stdio_work(ctx);
    do_pilot_work(ctx);

    if (deadline_met)
      ctx->update_deadline = make_timeout_time_ms(1000);
  }

  free(ctx);

  return 0;
}

int spi_main(void);

int
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

  const int max31856_init_status = max31856_init();
  if (max31856_init_status) {
    DEBUG_printf("max31856 init failed with %d\n", max31856_init_status);
    return max31856_init_status;
  }

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

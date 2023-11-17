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

typedef struct {
  struct tcp_pcb* server_pcb;
  struct tcp_pcb* client_pcb;
  uint8_t         recv_buffer[BUF_SIZE];
  uint16_t        recv_len; /* Received, valid bytes in recv_buffer */
} tcp_context_t;

typedef struct {
  absolute_time_t update_deadline;
  int             cur_temp;
  tcp_context_t   tcp;

  /*
   * Tells for how big chunk of a second furnace should be heating.
   * Each second is divided into 15 equal chunks.
   * For pwm_level number of chunks each second we will enable the heating.
   * For 10-pwm_level number of chunks each second we will disable the heating.
   *
   * pwm_current tells in which mode we are right now.
   * 1 - heating
   * 0 - not heating
   *
   * pwm_deadline holds 59 most significant bits of timestamp at which we
   * should switch heating state, ie. from heating to non-heating or from
   * non-heating to heating.
   *
   * TODO: Handle pwm_deadline overflow. Should we handle it?
   */
  uint64_t pwm_level    : 4;
  uint64_t pwm_current  : 1;
  uint64_t pwm_deadline : 59;
} furnace_context_t;

int max31856_init(void);

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

err_t
tcp_server_recv(void* ctx_, struct tcp_pcb* tpcb, struct pbuf* p, err_t err)
{
  furnace_context_t *ctx = (furnace_context_t*)ctx_;
  unsigned arg_pwm;

  if (!p) {
    tcp_server_close(ctx);
    return err;
  }

  tcp_server_recv_(ctx, tpcb, p);

  pbuf_free(p);

  // Echo back for debugging
  // tcp_server_send_data(ctx, tpcb, ctx->recv_buffer, ctx->recv_len);

  if (memcmp(ctx->tcp.recv_buffer, "reboot", 6) == 0) {
    reset_usb_boot(0,0);
  } else if (strncmp(ctx->tcp.recv_buffer, "pwm\n", 4) == 0) {
      char msg[16];
      const size_t msg_len = snprintf(msg, sizeof(msg), "pwm = %d\r\n", ctx->pwm_level);
      tcp_server_send_data(ctx, tpcb, msg, msg_len);
  } else if (sscanf(ctx->tcp.recv_buffer, "pwm %u", &arg_pwm) == 1) {
    if (arg_pwm > 15) {
      const char msg[] = "pwm argument too big!\r\n";
      const size_t msg_len = sizeof(msg)-1;
      tcp_server_send_data(ctx, tpcb, msg, msg_len);
    } else {
      ctx->pwm_level = arg_pwm;
    }
  }

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
  char temperature_str[16];

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
      "%d\n",
      ctx->cur_temp
    );

    tcp_server_send_data(
      ctx,
      ctx->tcp.client_pcb,
      (uint8_t*)temperature_str,
      temperature_str_len
    );
  }

}

void
do_pwm_work_maybe_switch(furnace_context_t *ctx)
{
  /* Get current time and "unpack" current pwm switch deadline. */
  const absolute_time_t current_time = get_absolute_time();
  const absolute_time_t current_deadline = ctx->pwm_deadline << 5;

  if (current_time < current_deadline)
    return;

  /* Divide 1000 microseconds into 15 equal chunks. */
  const absolute_time_t time_chunk = 1000 / 15;

  absolute_time_t new_duration;

  /*
   * If we were heating, we calculate for
   * how long we will now not be heating.
   */
  if (ctx->pwm_current)
    new_duration = time_chunk * (15 - ctx->pwm_level);

  /*
   * If we were not heating, we calculate for
   * how long we will now be heating.
   */
  else
    new_duration = time_chunk * (ctx->pwm_level);

  /* Calculate new timestamp and discard bottom 5 bits - we don't care. */
  new_duration += current_time;
  new_duration >>= 5;

  ctx->pwm_deadline = new_duration;
  ctx->pwm_current = !ctx->pwm_current;

  DEBUG_printf("HEAT: %d\n", (int) ctx->pwm_current);
}

void
do_pwm_work_(furnace_context_t *ctx)
{
  if (ctx->pwm_level == 0) {
    ctx->pwm_current = 0;
    return;
  }

  if (ctx->pwm_level == 15) {
    ctx->pwm_current = 1;
    return;
  }

  do_pwm_work_maybe_switch(ctx);
}

void
do_pwm_work(furnace_context_t *ctx)
{
  do_pwm_work_(ctx);

  gpio_put(FURNACE_FIRE_PIN, ctx->pwm_current);
}

int
main_work_loop(void)
{
  furnace_context_t* ctx;

  ctx = calloc(1, sizeof(furnace_context_t));

  if (!ctx) {
    return 1;
  }

  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

  while (1) {
    const bool deadline_met = get_absolute_time() > ctx->update_deadline;

    do_pwm_work(ctx);
    do_thermocouple_work(ctx, deadline_met);
    do_tcp_work(ctx, deadline_met);

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

  /* Setup GPIO for enabling and disabling heating. */
  gpio_init(FURNACE_FIRE_PIN);
  gpio_set_dir(FURNACE_FIRE_PIN, GPIO_OUT);
  gpio_put(FURNACE_FIRE_PIN, 0);

  while(1) {
    int ret = main_();

    if (ret)
      DEBUG_printf("main() failed with %d\n", ret);

    sleep_ms(3000);
  }
}

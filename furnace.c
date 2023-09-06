#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "pico/bootrom.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#define TCP_PORT        4242
#define DEBUG_printf    printf

typedef enum {
  STATE_DISCONNECTED = 0,
  STATE_LISTENING,
  STATE_CONNECTED,
} server_state_t;
#define BUF_SIZE        64

typedef struct {
  struct tcp_pcb* server_pcb;
  struct tcp_pcb* client_pcb;
  server_state_t  state;
  uint8_t         recv_buffer[BUF_SIZE];
} tcp_context_t;

typedef struct {
  absolute_time_t update_deadline;
  unsigned        cur_temp;
  tcp_context_t   tcp;
} furnace_context_t;

static err_t
tcp_server_close(furnace_context_t* ctx_)
{
  tcp_context_t * ctx = &ctx_->tcp;
  err_t err = ERR_OK;

  if (ctx->client_pcb != NULL) {
    tcp_arg(ctx->client_pcb, NULL);
    tcp_poll(ctx->client_pcb, NULL, 0);
    tcp_sent(ctx->client_pcb, NULL);
    tcp_recv(ctx->client_pcb, NULL);
    tcp_err(ctx->client_pcb, NULL);
    err = tcp_close(ctx->client_pcb);

    if (err != ERR_OK) {
      DEBUG_printf("close failed %d, calling abort\n", err);
      tcp_abort(ctx->client_pcb);
      err = ERR_ABRT;
    }

    ctx->client_pcb = NULL;
  }

  if (ctx->server_pcb) {
    tcp_arg(ctx->server_pcb, NULL);
    tcp_close(ctx->server_pcb);
    ctx->server_pcb = NULL;
  }

  return err;
}

static err_t
tcp_server_sent(void* ctx_, struct tcp_pcb* tpcb, u16_t len)
{
  furnace_context_t* ctx = (furnace_context_t*) ctx_;

  return ERR_OK;
}

err_t
tcp_server_send_data(furnace_context_t* ctx,
                     struct tcp_pcb*    tpcb,
                     uint8_t*           data,
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

  // Receive the buffer
  const uint16_t buffer_left = BUF_SIZE;
  pbuf_copy_partial(p, ctx->tcp.recv_buffer, p->tot_len > buffer_left ? buffer_left : p->tot_len, 0);
  tcp_recved(tpcb, p->tot_len);
}

err_t
tcp_server_recv(void* ctx_, struct tcp_pcb* tpcb, struct pbuf* p, err_t err)
{
  furnace_context_t *ctx = (furnace_context_t*)ctx_;

  if (!p) {
    tcp_server_close(ctx);
    return err;
  }

  tcp_server_recv_(ctx, tpcb, p);

  pbuf_free(p);

  // Echo back for debugging
  // tcp_server_send_data(ctx, tpcb, ctx->recv_buffer, ctx->recv_len);

  if (memcmp(ctx->tcp.recv_buffer, "reboot", 6) == 0)
    reset_usb_boot(0,0);

  DEBUG_printf("tcp_server_recv: %.*s\n", p->tot_len, ctx->tcp.recv_buffer);

  return ERR_OK;
}

static void
tcp_server_err(void* ctx_, err_t err)
{
  furnace_context_t *ctx = (furnace_context_t*)ctx_;

  if (err != ERR_ABRT) {
    DEBUG_printf("tcp_client_err_fn %d\n", err);
    ctx->tcp.state = STATE_DISCONNECTED;
    tcp_server_close(ctx);
  }
}

static err_t
tcp_server_accept(void* ctx_, struct tcp_pcb* client_pcb, err_t err)
{
  furnace_context_t *ctx = (furnace_context_t*)ctx_;

  if (err != ERR_OK || client_pcb == NULL) {
    DEBUG_printf("Failure in accept\n");
    ctx->tcp.state = STATE_DISCONNECTED;
    tcp_server_close(ctx);
    return ERR_VAL;
  }

  DEBUG_printf("Client connected\n");

  ctx->tcp.client_pcb = client_pcb;
  tcp_arg(client_pcb, ctx);
  tcp_sent(client_pcb, tcp_server_sent);
  tcp_recv(client_pcb, tcp_server_recv);
  tcp_err(client_pcb, tcp_server_err);

  ctx->tcp.state = STATE_CONNECTED;
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

  ctx->tcp.state = STATE_LISTENING;
  tcp_arg(ctx->tcp.server_pcb, ctx);
  tcp_accept(ctx->tcp.server_pcb, tcp_server_accept);

  return true;
}

void
do_thermocouple_work(furnace_context_t *ctx, bool deadline_met)
{
  if (!deadline_met)
    return;

  // fake work
  sleep_ms(100);

  ctx->cur_temp = 1337;
}

void
do_tcp_work(furnace_context_t *ctx, bool deadline_met)
{
  char temperature_str[32];

  cyw43_arch_poll();

  // If disconnected, reset and setup listening
  if (ctx->tcp.state == STATE_DISCONNECTED) {
    memset(&ctx->tcp, 0, sizeof(ctx->tcp));
    if (!tcp_server_open(ctx)) {
      tcp_server_close(ctx);
    }
  }

  if (ctx->tcp.client_pcb && deadline_met) {
    const int temperature_str_len = snprintf(
      temperature_str,
      sizeof(temperature_str),
      "%u\n",
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

int
run_tcp_server_test(void)
{
  furnace_context_t* ctx;

  ctx = calloc(1, sizeof(furnace_context_t));

  if (!ctx) {
    return 1;
  }

  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

  while (1) {
    const bool deadline_met = get_absolute_time() > ctx->update_deadline;

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
  stdio_init_all();

  if (cyw43_arch_init()) {
    DEBUG_printf("failed to initialise\n");
    return 1;
  }

  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

  cyw43_arch_enable_sta_mode();

  DEBUG_printf("Connecting to Wi-Fi...\n");

  if (cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
    DEBUG_printf("failed to connect.\n");
    return 1;
  } else {
    DEBUG_printf("Connected.\n");
  }

  const int ret = run_tcp_server_test();

  cyw43_arch_deinit();

  return ret;
}

int
main(void)
{
  while(1) {
    int ret = main_();

    if (ret)
      DEBUG_printf("main() failed with %d\n", ret);

    sleep_ms(3000);
  }
}

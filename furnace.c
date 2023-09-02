#include <time.h>
#include <stdlib.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

#include "lwip/pbuf.h"
#include "lwip/tcp.h"

#define TCP_PORT        4242
#define DEBUG_printf    printf
#define BUF_SIZE        512

typedef enum {
  STATE_DISCONNECTED = 0,
  STATE_LISTENING,
  STATE_CONNECTED,
} server_state_t;

typedef struct {
  struct tcp_pcb* server_pcb;
  struct tcp_pcb* client_pcb;
  server_state_t  state;
  uint8_t         recv_buffer[BUF_SIZE];
  u16_t           submitted_len;
  u16_t           recv_len;
} tcp_server_context_t;

static err_t
tcp_server_close(tcp_server_context_t* ctx)
{
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
  tcp_server_context_t* ctx = (tcp_server_context_t*) ctx_;

  return ERR_OK;
}

err_t
tcp_server_send_data(tcp_server_context_t* ctx,
                     struct tcp_pcb*       tpcb,
                     uint8_t*              data,
                     u16_t                 size)
{
  return tcp_write(tpcb, data, size, TCP_WRITE_FLAG_COPY);
}

err_t
tcp_server_recv(void* ctx_, struct tcp_pcb* tpcb, struct pbuf* p, err_t err)
{
  tcp_server_context_t* ctx = (tcp_server_context_t*)ctx_;

  if (!p) {
    tcp_server_close(ctx);
    return err;
  }

  if (p->tot_len > 0) {
    DEBUG_printf(
      "tcp_server_recv %d/%d err %d\n", p->tot_len, ctx->recv_len, err);

    // Receive the buffer
    const uint16_t buffer_left = BUF_SIZE - ctx->recv_len;
    ctx->recv_len +=
      pbuf_copy_partial(p,
                        ctx->recv_buffer + ctx->recv_len,
                        p->tot_len > buffer_left ? buffer_left : p->tot_len,
                        0);
    tcp_recved(tpcb, p->tot_len);
  }

  pbuf_free(p);

  // Echo back for debugging
  tcp_server_send_data(ctx, tpcb, ctx->recv_buffer, ctx->recv_len);

  ctx->recv_len = 0;

  return ERR_OK;
}

static void
tcp_server_err(void* ctx_, err_t err)
{
  tcp_server_context_t *ctx = (tcp_server_context_t*) ctx_;

  if (err != ERR_ABRT) {
    DEBUG_printf("tcp_client_err_fn %d\n", err);
    ctx->state = STATE_DISCONNECTED;
    tcp_server_close(ctx);
  }
}

static err_t
tcp_server_accept(void* ctx_, struct tcp_pcb* client_pcb, err_t err)
{
  tcp_server_context_t* ctx = (tcp_server_context_t*) ctx_;

  if (err != ERR_OK || client_pcb == NULL) {
    DEBUG_printf("Failure in accept\n");
    ctx->state = STATE_DISCONNECTED;
    tcp_server_close(ctx);
    return ERR_VAL;
  }

  DEBUG_printf("Client connected\n");

  ctx->client_pcb = client_pcb;
  tcp_arg(client_pcb, ctx);
  tcp_sent(client_pcb, tcp_server_sent);
  tcp_recv(client_pcb, tcp_server_recv);
  tcp_err(client_pcb, tcp_server_err);

  ctx->state = STATE_CONNECTED;
  return ERR_OK;
}

static bool
tcp_server_open(void* ctx_)
{
  tcp_server_context_t* ctx = (tcp_server_context_t*) ctx_;
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

  ctx->server_pcb = tcp_listen_with_backlog(pcb, 1);
  if (!ctx->server_pcb) {
    DEBUG_printf("failed to listen\n");
    if (pcb) {
      tcp_close(pcb);
    }
    return false;
  }

  ctx->state = STATE_LISTENING;
  tcp_arg(ctx->server_pcb, ctx);
  tcp_accept(ctx->server_pcb, tcp_server_accept);

  return true;
}

static unsigned
thermocouple_get(void)
{
  // fake work
  sleep_ms(100);

  return 1337;
}

void
run_tcp_server_test(void)
{
  tcp_server_context_t* ctx;
  char temperature_str[32];

  ctx = calloc(1, sizeof(tcp_server_context_t));

  if (!ctx) {
    return;
  }

  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);

  while (1) {
    memset(ctx, 0, sizeof(*ctx));
    if (!tcp_server_open(ctx)) {
      tcp_server_close(ctx);
      sleep_ms(2000);
      continue;
    }

    // Deadline on which we have to get the measurements and update the user
    absolute_time_t update_deadline = make_timeout_time_ms(1000);

    while (ctx->state != STATE_DISCONNECTED) {
      cyw43_arch_wait_for_work_until(update_deadline);
      cyw43_arch_poll();

      if (ctx->client_pcb && get_absolute_time() > update_deadline) {
        update_deadline = make_timeout_time_ms(3000);

        const unsigned temperature = thermocouple_get();
        const int temperature_str_len = snprintf(
          temperature_str, sizeof(temperature_str), "%" PRId64 "\n", get_absolute_time());

        tcp_server_send_data(ctx,
                             ctx->client_pcb,
                             (uint8_t*)temperature_str,
                             temperature_str_len);
      }

    }

  }

  free(ctx);
}

int
main(void)
{
  stdio_init_all();

  if (cyw43_arch_init()) {
    printf("failed to initialise\n");
    return 1;
  }

  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

  cyw43_arch_enable_sta_mode();

  printf("Connecting to Wi-Fi...\n");

  if (cyw43_arch_wifi_connect_timeout_ms(
        WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
    printf("failed to connect.\n");
    return 1;
  } else {
    printf("Connected.\n");
  }

  run_tcp_server_test();

  cyw43_arch_deinit();

  return 0;
}

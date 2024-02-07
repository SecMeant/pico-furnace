#define MAGNETRON_PIN       10
#define MAGNETRON_PULSE_MS  50

static void
do_magnetron_work(furnace_context_t *ctx, bool deadline_met)
{
  if(!deadline_met || ctx->pulse_count == 0)
    return;

  const int timeout_ms = ctx->pulse_count%2 == 0 ? MAGNETRON_PULSE_MS : (1000-MAGNETRON_PULSE_MS);
  gpio_put(MAGNETRON_PIN, ctx->pulse_count%2 == 0);
  ctx->magnetron_deadline = make_timeout_time_ms(timeout_ms);

  ctx->pulse_count--;
}

static void
init_magnetron(furnace_context_t *ctx)
{
  ctx->pulse_count = 0;
  gpio_init(MAGNETRON_PIN);
  gpio_set_dir(MAGNETRON_PIN, GPIO_OUT);
  ctx->magnetron_deadline = make_timeout_time_ms(1000);
}

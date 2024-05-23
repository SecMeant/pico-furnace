#include "spin_coater.h"
#include "spin_coater_dshot.h"

#include "hardware/pio.h"

static PIO pio = pio0;
static int pio_sm = -1;

bool
dshot_init(uint16_t dshot_gpio)
{
  uint pio_offset = pio_add_program(pio, &dshot_encoder_program);
  pio_sm = pio_claim_unused_sm(pio, true);

  if (pio_sm < 0) {
    pio_sm_unclaim(pio, pio_sm);
    return false;
  }

  dshot_encoder_program_init(pio, pio_sm, pio_offset, dshot_gpio);
  return true;
}

void
dshot_send_command(uint16_t c)
{
  // Shift for telemetry bit (0)
  c = c << 1;

  // Shift and include checksum
  uint16_t checksum = (c ^ (c >> 4) ^ (c >> 8)) & 0x0F;
  c = (c << 4) | checksum;

  pio_sm_put_blocking(pio, pio_sm, c);
}
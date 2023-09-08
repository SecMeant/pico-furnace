#include <stdint.h>

#include "spi_config.h"

#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

/*
 * MAX31856 Precision Thermocouple to Digital Converter with Linearization.
 * Docs: https://holzcoredump.cc/MAX31856.pdf
 */

#define MAX31856_REG_READ_BIT (0x00)
#define MAX31856_REG_WRITE_BIT (0x80)

#define MAX31856_REG_CR0  (0x00)
#define MAX31856_REG_CR1  (0x01)
#define MAX31856_REG_MASK (0x02)
#define MAX31856_REG_CJHF (0x03)
#define MAX31856_REG_CJLF (0x04)

#define MAX31856_SPI_INSTANCE FURNACE_SPI_INSTANCE

void max31856_spi_init(void);

static inline uint8_t max31856_read_reg8(uint8_t addr)
{
  const uint16_t len = 2;
  uint8_t src[len], dst[len];

  src[0] = MAX31856_REG_READ_BIT | addr;
  src[1] = 0;

  gpio_put(FURNACE_SPI_CSN_PIN, 0);
  spi_write_read_blocking(MAX31856_SPI_INSTANCE, src, dst, len);
  gpio_put(FURNACE_SPI_CSN_PIN, 1);

  return dst[1];
}

static inline uint8_t max31856_write_reg8(uint8_t addr, uint8_t val)
{
  const uint16_t len = 2;
  uint8_t src[len], dst[len];

  src[0] = MAX31856_REG_WRITE_BIT | addr;
  src[1] = val;

  gpio_put(FURNACE_SPI_CSN_PIN, 0);
  spi_write_read_blocking(MAX31856_SPI_INSTANCE, src, dst, len);
  gpio_put(FURNACE_SPI_CSN_PIN, 1);

  return dst[1];
}

int max31856_init(void)
{
  max31856_spi_init();

  /* 
   * Sanity check.
   * We read few first registers and check against default values.
   * It's basically SPI connection check.
   */
  if (max31856_read_reg8(MAX31856_REG_CR0) != 0x00)
    return 1;
  if (max31856_read_reg8(MAX31856_REG_CR1) != 0x03)
    return 1;
  if (max31856_read_reg8(MAX31856_REG_MASK) != 0xff)
    return 1;
  if (max31856_read_reg8(MAX31856_REG_CJHF) != 0x7f)
    return 1;
  if (max31856_read_reg8(MAX31856_REG_CJLF) != 0xc0)
    return 1;

  /* Enable automatic conversion mode and 50Hz noise filter. */
  max31856_write_reg8(MAX31856_REG_CR0, 0x81);

  /* Set thermocouple type to S */
  max31856_write_reg8(MAX31856_REG_CR1, 0x06);

  return 0;
}



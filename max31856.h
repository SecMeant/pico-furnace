#pragma once

#include <string.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"

#include "spi_config.h"

#define MAX31856_REG_READ_BIT (0x00)
#define MAX31856_REG_WRITE_BIT (0x80)

#define MAX31856_REG_CR0   (0x00)
#define MAX31856_REG_CR1   (0x01)
#define MAX31856_REG_MASK  (0x02)
#define MAX31856_REG_CJHF  (0x03)
#define MAX31856_REG_CJLF  (0x04)
#define MAX31856_REG_CJTO  (0x09)
#define MAX31856_REG_CJTH  (0x0a)
#define MAX31856_REG_CJTL  (0x0b)
#define MAX31856_REG_LTCBH (0x0c)
#define MAX31856_REG_LTCBM (0x0d)
#define MAX31856_REG_LTCBL (0x0e)

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

  sleep_ms(1);

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

  sleep_ms(1);

  return dst[1];
}

static inline bool max31856_read_data_ready(void)
{
  return gpio_get(FURNACE_MAX31856_RDY);
}

static inline uint32_t max31856_read_cold_junction(void)
{
  const uint16_t len = 2;
  uint8_t src[len], dst[len];

  memset(src, 0, len);
  src[0] = MAX31856_REG_CJTH;

  gpio_put(FURNACE_SPI_CSN_PIN, 0);
  spi_write_read_blocking(MAX31856_SPI_INSTANCE, src, dst, len);
  gpio_put(FURNACE_SPI_CSN_PIN, 1);

  sleep_ms(1);

  return dst[1];
}

static inline int max31856_read_temperature(void)
{
  const uint16_t len = 4;
  uint8_t src[len], dst[len];
  int temperature = 0;

  _Static_assert(MAX31856_REG_LTCBH + 1 == MAX31856_REG_LTCBM);
  _Static_assert(MAX31856_REG_LTCBH + 2 == MAX31856_REG_LTCBL);
  memset(src, 0, len);
  src[0] = MAX31856_REG_LTCBH;

  gpio_put(FURNACE_SPI_CSN_PIN, 0);
  spi_write_read_blocking(MAX31856_SPI_INSTANCE, src, dst, len);
  gpio_put(FURNACE_SPI_CSN_PIN, 1);

  sleep_ms(1);

  /*
   * Sign extend ltcbh and zero extend the rest.
   * TODO: convert bits after the decimal point too.
   */
  const int32_t  ltcbh = (int8_t) dst[1];
  const uint32_t ltcbm = dst[2], ltcbl = dst[3];
  temperature = (((uint32_t) ltcbh) << 4)
              | (((uint32_t) ltcbm) >> 4);

  (void) ltcbl;

  return temperature;
}


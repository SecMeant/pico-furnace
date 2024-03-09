#pragma once

#include <string.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "pico/binary_info.h"
#include "hardware/spi.h"

#include "spi_config.h"

#define MAX318xx_REG_READ_BIT (0x00)
#define MAX318xx_REG_WRITE_BIT (0x80)

#define MAX318xx_SPI_INSTANCE FURNACE_SPI_INSTANCE

void max318xx_spi_init(void);

static inline uint8_t max318xx_read_reg8(uint8_t addr)
{
  const uint16_t len = 2;
  uint8_t src[len], dst[len];

  src[0] = MAX318xx_REG_READ_BIT | addr;
  src[1] = 0;

  gpio_put(FURNACE_SPI_CSN_PIN, 0);
  spi_write_read_blocking(MAX318xx_SPI_INSTANCE, src, dst, len);
  gpio_put(FURNACE_SPI_CSN_PIN, 1);

  sleep_ms(1);

  return dst[1];
}

static inline uint8_t max318xx_write_reg8(uint8_t addr, uint8_t val)
{
  const uint16_t len = 2;
  uint8_t src[len], dst[len];

  src[0] = MAX318xx_REG_WRITE_BIT | addr;
  src[1] = val;

  gpio_put(FURNACE_SPI_CSN_PIN, 0);
  spi_write_read_blocking(MAX318xx_SPI_INSTANCE, src, dst, len);
  gpio_put(FURNACE_SPI_CSN_PIN, 1);

  sleep_ms(1);

  return dst[1];
}

#if CONFIG_THERMO == CONFIG_THERMO_PT100
  #include "max31865.h"
#elif CONFIG_THERMO == CONFIG_THERMO_KTYPE
  #include "max31856.h"
#elif CONFIG_THERMO >= 1
    #error "Invalid CONFIG_THERMO value"
#endif

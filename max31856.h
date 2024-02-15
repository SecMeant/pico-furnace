#pragma once

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

/*
 * MAX31856 Precision Thermocouple to Digital Converter with Linearization.
 * Docs: https://holzcoredump.cc/MAX31856.pdf
 */

static inline uint32_t max318xx_read_cold_junction(void)
{
  const uint16_t len = 2;
  uint8_t src[len], dst[len];

  memset(src, 0, len);
  src[0] = MAX31856_REG_CJTH;

  gpio_put(FURNACE_SPI_CSN_PIN, 0);
  spi_write_read_blocking(MAX318xx_SPI_INSTANCE, src, dst, len);
  gpio_put(FURNACE_SPI_CSN_PIN, 1);

  sleep_ms(1);

  return dst[1];
}

static inline int max318xx_read_temperature(void)
{
  const uint16_t len = 4;
  uint8_t src[len], dst[len];
  int temperature = 0;

  _Static_assert(MAX31856_REG_LTCBH + 1 == MAX31856_REG_LTCBM);
  _Static_assert(MAX31856_REG_LTCBH + 2 == MAX31856_REG_LTCBL);
  memset(src, 0, len);
  src[0] = MAX31856_REG_LTCBH;

  gpio_put(FURNACE_SPI_CSN_PIN, 0);
  spi_write_read_blocking(MAX318xx_SPI_INSTANCE, src, dst, len);
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

static inline void max318xx_config(void)
{
  printf("Initializing MAX31856...");

  max318xx_spi_init();

  /* Configure GPIO pin that we can attach to RDY pin on MAX 318xx */
  gpio_init(FURNACE_MAX318xx_RDY);
  gpio_set_dir(FURNACE_MAX318xx_RDY, GPIO_IN);

  /* Enable automatic conversion mode and 50Hz noise filter. */
  max318xx_write_reg8(MAX31856_REG_CR0, 0x81);

  /* Set thermocouple type to S */
  // max31856_write_reg8(MAX31856_REG_CR1, 0x06);

  /* Set thermocouple type to K */
  max318xx_write_reg8(MAX31856_REG_CR1, 0x03);
}

static inline int max318xx_sanity_check(void)
{
  /* 
   * Sanity check.
   * We read few first registers and check against default values.
   * It's basically SPI connection check.
   */
  if (max318xx_read_reg8(MAX31856_REG_CR0) != 0x81)
    return 1;
  if (max318xx_read_reg8(MAX31856_REG_CR1) != 0x03)
    return 2;
  if (max318xx_read_reg8(MAX31856_REG_MASK) != 0xff)
    return 3;
  if (max318xx_read_reg8(MAX31856_REG_CJHF) != 0x7f)
    return 4;
  if (max318xx_read_reg8(MAX31856_REG_CJLF) != 0xc0)
    return 5;

  printf(" OK\n");

  return 0;
}

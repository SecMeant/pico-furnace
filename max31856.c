#include <stdio.h>

#include "max31856.h"

/*
 * MAX31856 Precision Thermocouple to Digital Converter with Linearization.
 * Docs: https://holzcoredump.cc/MAX31856.pdf
 */

int max31856_init(void)
{
  printf("Initializing MAX31856...");

  max31856_spi_init();

  /* Configure GPIO pin that we can attach to RDY pin on MAX 31856 */
  gpio_init(FURNACE_MAX31856_RDY);
  gpio_set_dir(FURNACE_MAX31856_RDY, GPIO_IN);

  /* Enable automatic conversion mode and 50Hz noise filter. */
  max31856_write_reg8(MAX31856_REG_CR0, 0x81);

  /* Set thermocouple type to S */
  max31856_write_reg8(MAX31856_REG_CR1, 0x06);

  /* 
   * Sanity check.
   * We read few first registers and check against default values.
   * It's basically SPI connection check.
   */
  if (max31856_read_reg8(MAX31856_REG_CR0) != 0x81)
    return 1;
  if (max31856_read_reg8(MAX31856_REG_CR1) != 0x06)
    return 2;
  if (max31856_read_reg8(MAX31856_REG_MASK) != 0xff)
    return 3;
  if (max31856_read_reg8(MAX31856_REG_CJHF) != 0x7f)
    return 4;
  if (max31856_read_reg8(MAX31856_REG_CJLF) != 0xc0)
    return 5;

  printf(" OK\n");

  return 0;
}



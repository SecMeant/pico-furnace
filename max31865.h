#pragma once

#include <math.h>

#define R_REF           430
#define RTD_NOMINAL     100
#define CASUAL_WIRE_RES 2
#define MAX_ADC_VALUE   1U << 15

//Those are constants used to calculate temperature based on resistance
//source : https://www.analog.com/media/en/technical-documentation/application-notes/AN709_0.pdf, page 4

#define Z1          -3.9083e-3
#define Z2          1.758480889e-5
#define Z3          -2.36e-8
#define Z4          -1.155e-6

#define MAX31865_REG_CONF           (0x00)
#define MAX31865_REG_RTD_MSB        (0x01)
#define MAX31865_REG_RTD_LSB        (0x02)
#define MAX31865_REG_HFT_MSB        (0x03)
#define MAX31865_REG_HFT_LSB        (0x04)
#define MAX31865_REG_LFT_MSB        (0x05)
#define MAX31865_REG_LFT_LSB        (0x06)
#define MAX31865_REG_FS             (0x07)

static inline uint32_t max318xx_read_cold_junction(void)
{
  return 0;
}

static inline int max318xx_read_temperature(void)
{
  const uint16_t len = 3;
  uint8_t src[len], dst[len];

  _Static_assert(MAX31865_REG_RTD_MSB + 1 == MAX31865_REG_RTD_LSB);
  memset(src, 0, len);
  src[0] = MAX31865_REG_RTD_MSB;

  gpio_put(FURNACE_SPI_CSN_PIN, 0);
  spi_write_read_blocking(MAX318xx_SPI_INSTANCE, src, dst, len);
  gpio_put(FURNACE_SPI_CSN_PIN, 1);

  sleep_ms(1);

  const uint32_t rtd_msb = dst[1], rtd_lsb = dst[2];

  if(rtd_lsb & 0b01)
    printf("max31865: Fault detected!!\n");

  // LSB bit of RTD_LSB reg is acutally a fault bit thus we don't want to include it in read value.
  const uint32_t adc_read = (uint32_t)( rtd_msb << 7 ) | (uint32_t)( rtd_lsb >> 1 );
  const float r_rtd = ((float)(adc_read* R_REF) / (MAX_ADC_VALUE)) - CASUAL_WIRE_RES;

  // We don't expect values less than 0 Celcius.
  // If we would like to handle this, there is different equation.
  float temperature_f = (sqrt(Z2 + Z3*r_rtd) + Z1)/Z4;

  return (int) temperature_f;
}

static inline void max318xx_config(void)
{
  printf("Initializing MAX31865...");

  max318xx_spi_init();

  /* Configure GPIO pin that we can attach to RDY pin on MAX 318xx */
  gpio_init(FURNACE_MAX318xx_RDY);
  gpio_set_dir(FURNACE_MAX318xx_RDY, GPIO_IN);

  /* Enable automatic conversion mode, Vbias and 50Hz noise filter. */
  max318xx_write_reg8(MAX31865_REG_CONF, 0xC1);
}

static inline int max318xx_sanity_check(void)
{
  if (max318xx_read_reg8(MAX31865_REG_HFT_MSB) != 0xFF)
    return 1;
  if (max318xx_read_reg8(MAX31865_REG_HFT_LSB) != 0xFF)
    return 2;
  if (max318xx_read_reg8(MAX31865_REG_LFT_MSB) != 0x00)
    return 3;
  if (max318xx_read_reg8(MAX31865_REG_LFT_LSB) != 0x00)
    return 4;
  if (max318xx_read_reg8(MAX31865_REG_CONF)    != 0xC1)
    return 5;

  printf(" OK\n");

  return 0;
}

#include <stdio.h>
#include <stdlib.h>

#include "spi_config.h"

#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "hardware/dma.h"

#define TEST_SIZE 1024

#define DMA 0

void max31856_spi_init(void)
{
    /* Enable SPI at 1 MHz and connect to GPIOs */
    spi_init(FURNACE_SPI_INSTANCE, 1000 * 512);
    gpio_set_function(FURNACE_SPI_RX_PIN, GPIO_FUNC_SPI);

    /*
     * We set chipselect to gpio to make sure It's pulled up/down when we want.
     * By default raspberry was pulling the CS up after each byte.
     */
    gpio_init(FURNACE_SPI_CSN_PIN);
    gpio_set_dir(FURNACE_SPI_CSN_PIN, GPIO_OUT);
    gpio_put(FURNACE_SPI_CSN_PIN, 1);

    gpio_set_function(FURNACE_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(FURNACE_SPI_TX_PIN, GPIO_FUNC_SPI);

    spi_set_format(FURNACE_SPI_INSTANCE, 8, SPI_CPOL_0, SPI_CPHA_1, SPI_MSB_FIRST);

    // Force loopback for testing
    // hw_set_bits(&spi_get_hw(FURNACE_SPI_INSTANCE)->cr1, SPI_SSPCR1_LBM_BITS);
}

int spi_main()
{

    printf("SPI DMA example\n");

    max31856_spi_init();

#if DMA
    // Grab some unused dma channels
    const uint dma_tx = dma_claim_unused_channel(true);
    const uint dma_rx = dma_claim_unused_channel(true);
#endif

    static uint8_t txbuf[TEST_SIZE];
    static uint8_t rxbuf[TEST_SIZE];
    for (uint i = 0; i < TEST_SIZE; ++i) {
        txbuf[i] = i;
    }

#if DMA

    // We set the outbound DMA to transfer from a memory buffer to the SPI transmit FIFO paced by the SPI TX FIFO DREQ
    // The default is for the read address to increment every element (in this case 1 byte = DMA_SIZE_8)
    // and for the write address to remain unchanged.

    printf("Configure TX DMA\n");
    dma_channel_config c = dma_channel_get_default_config(dma_tx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, spi_get_dreq(FURNACE_SPI_INSTANCE, true));
    dma_channel_configure(dma_tx, &c,
                          &spi_get_hw(FURNACE_SPI_INSTANCE)->dr, // write address
                          txbuf, // read address
                          TEST_SIZE, // element count (each element is of size transfer_data_size)
                          false); // don't start yet

    printf("Configure RX DMA\n");

    // We set the inbound DMA to transfer from the SPI receive FIFO to a memory buffer paced by the SPI RX FIFO DREQ
    // We configure the read address to remain unchanged for each element, but the write
    // address to increment (so data is written throughout the buffer)
    c = dma_channel_get_default_config(dma_rx);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
    channel_config_set_dreq(&c, spi_get_dreq(FURNACE_SPI_INSTANCE, false));
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    dma_channel_configure(dma_rx, &c,
                          rxbuf, // write address
                          &spi_get_hw(FURNACE_SPI_INSTANCE)->dr, // read address
                          TEST_SIZE, // element count (each element is of size transfer_data_size)
                          false); // don't start yet

    printf("Starting DMAs...\n");
    // start them exactly simultaneously to avoid races (in extreme cases the FIFO could overflow)
    dma_start_channel_mask((1u << dma_tx) | (1u << dma_rx));
    printf("Wait for RX complete...\n");
    dma_channel_wait_for_finish_blocking(dma_rx);
    if (dma_channel_is_busy(dma_tx)) {
        panic("RX completed before TX");
    }

#else

    spi_write_read_blocking(FURNACE_SPI_INSTANCE, txbuf, rxbuf, TEST_SIZE);

#endif

    printf("Done. Checking...");
    for (uint i = 0; i < TEST_SIZE; ++i) {
        if (rxbuf[i] != txbuf[i]) {
            panic("Mismatch at %d/%d: expected %02x, got %02x",
                  i, TEST_SIZE, txbuf[i], rxbuf[i]
            );
        }
    }

    printf("All good\n");

#if DMA
    dma_channel_unclaim(dma_tx);
    dma_channel_unclaim(dma_rx);
#endif

    while(1) {
        printf(".");
        sleep_ms(1000);
    }

    return 0;
}

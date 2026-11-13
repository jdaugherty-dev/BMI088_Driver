/*
 * Raw SPI diagnostic: reads the chip-ID register directly off each die,
 * bypassing bmi08xa_init()'s strict chip-ID check, so a wiring problem
 * shows up as a specific byte value instead of just "-3 device not found".
 *
 *   0x1E  -- accel responding correctly (BMI088)
 *   0x0F  -- gyro responding correctly
 *   0x00  -- MISO stuck low: likely no power, or that CS line isn't
 *            actually reaching the chip (dead bus from this chip's POV)
 *   0xFF  -- MISO stuck high: likely an open/floating line, often a
 *            CS or MISO connection that isn't making contact
 *   anything else -- something is responding, just not the right chip ID;
 *            check for CS1/CS2 swapped, or SCK/MOSI/MISO miswired
 */
#include <stdio.h>

#include "rpi_platform.h"

#define SPI_DEV       "/dev/spidev0.0"
#define GPIOCHIP      "/dev/gpiochip0"
#define ACCEL_CS_GPIO 24u /* CS1, header physical pin 18 */
#define GYRO_CS_GPIO  23u /* CS2, header physical pin 16 */

#define BMI08_SPI_RD_MASK 0x80u
#define BMI08_REG_CHIP_ID 0x00u

int main(void)
{
    struct rpi_bmi08x_cs accel_cs, gyro_cs;

    if (rpi_bmi08x_platform_init(&accel_cs, &gyro_cs, SPI_DEV, GPIOCHIP,
                                  ACCEL_CS_GPIO, GYRO_CS_GPIO) != 0) {
        fprintf(stderr, "platform init failed\n");
        return 1;
    }

    uint8_t chip_id = 0;
    uint8_t reg = BMI08_REG_CHIP_ID | BMI08_SPI_RD_MASK;

    /*
     * The accel die's first SPI transaction after power-on both switches
     * it from its default I2C mode into SPI mode AND returns garbage; the
     * real chip-ID only shows up starting on the second read. This matches
     * what bmi08a_init() itself does internally.
     */
    rpi_bmi08x_spi_read(reg, &chip_id, 1, &accel_cs);
    int accel_rslt = rpi_bmi08x_spi_read(reg, &chip_id, 1, &accel_cs);
    printf("accel chip id register: 0x%02X (read status %d, expect 0x1E)\n", chip_id, accel_rslt);

    chip_id = 0;
    int gyro_rslt = rpi_bmi08x_spi_read(reg, &chip_id, 1, &gyro_cs);
    printf("gyro  chip id register: 0x%02X (read status %d, expect 0x0F)\n", chip_id, gyro_rslt);

    rpi_bmi08x_platform_deinit(&accel_cs);

    return 0;
}

/*
 * Linux spidev + libgpiod platform glue for the Bosch BMI08x_SensorAPI on a
 * Raspberry Pi 4 Model B.
 *
 * The breakout board wires the BMI088 to the full 40-pin GPIO header:
 *   SCK  -> physical pin 23 (GPIO11, SPI0 SCLK)
 *   MOSI -> physical pin 19 (GPIO10, SPI0 MOSI)
 *   MISO -> physical pin 21 (GPIO9,  SPI0 MISO)
 *   CS1  -> physical pin 18 (GPIO24) -- accelerometer chip select
 *   CS2  -> physical pin 16 (GPIO23) -- gyroscope chip select
 *   PS   -> tied to GND on the board (selects SPI mode for the gyro die)
 *
 * CS1/CS2 are NOT on the Pi's hardware CE0/CE1 lines, so spidev's automatic
 * chip select is disabled (SPI_NO_CS) and both CS lines are toggled by hand
 * through libgpiod around each transfer.
 */
#ifndef RPI_PLATFORM_H
#define RPI_PLATFORM_H

#include "bmi08.h"

struct rpi_bmi08x_cs {
    int spi_fd;
    struct gpiod_line_request *cs_req;
    unsigned int cs_offset;
};

/*
 * Opens spi_dev_path (e.g. "/dev/spidev0.0") and requests accel_cs_offset /
 * gyro_cs_offset as outputs on gpiochip_path (e.g. "/dev/gpiochip0").
 * Fills in accel_ctx and gyro_ctx, which are then pointed to by
 * dev->intf_ptr_accel / dev->intf_ptr_gyro.
 *
 * Returns 0 on success, -1 on failure (errno / perror set).
 */
int rpi_bmi08x_platform_init(struct rpi_bmi08x_cs *accel_ctx,
                              struct rpi_bmi08x_cs *gyro_ctx,
                              const char *spi_dev_path,
                              const char *gpiochip_path,
                              unsigned int accel_cs_offset,
                              unsigned int gyro_cs_offset);

/* Releases the gpiod request and closes the spi file descriptor. */
void rpi_bmi08x_platform_deinit(struct rpi_bmi08x_cs *accel_ctx);

/* bmi08_read_fptr_t / bmi08_write_fptr_t / bmi08_delay_us_fptr_t */
BMI08_INTF_RET_TYPE rpi_bmi08x_spi_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr);
BMI08_INTF_RET_TYPE rpi_bmi08x_spi_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr);
void rpi_bmi08x_delay_us(uint32_t period, void *intf_ptr);

#endif /* RPI_PLATFORM_H */

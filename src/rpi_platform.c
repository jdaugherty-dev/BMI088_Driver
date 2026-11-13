#include "rpi_platform.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <gpiod.h>

#define BMI08X_SPI_SPEED_HZ   5000000u
#define BMI08X_MAX_XFER_LEN   64u

int rpi_bmi08x_platform_init(struct rpi_bmi08x_cs *accel_ctx,
                              struct rpi_bmi08x_cs *gyro_ctx,
                              const char *spi_dev_path,
                              const char *gpiochip_path,
                              unsigned int accel_cs_offset,
                              unsigned int gyro_cs_offset)
{
    int fd = open(spi_dev_path, O_RDWR);

    if (fd < 0) {
        perror("open spidev");
        return -1;
    }

    uint8_t mode = SPI_MODE_3 | SPI_NO_CS;
    uint8_t bits = 8;
    uint32_t speed = BMI08X_SPI_SPEED_HZ;

    if (ioctl(fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("configure spidev");
        close(fd);
        return -1;
    }

    struct gpiod_chip *chip = gpiod_chip_open(gpiochip_path);

    if (!chip) {
        perror("gpiod_chip_open");
        close(fd);
        return -1;
    }

    struct gpiod_line_settings *settings = gpiod_line_settings_new();

    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_active_low(settings, true);
    gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);

    unsigned int offsets[2] = { accel_cs_offset, gyro_cs_offset };

    struct gpiod_line_config *line_cfg = gpiod_line_config_new();

    gpiod_line_config_add_line_settings(line_cfg, offsets, 2, settings);

    struct gpiod_request_config *req_cfg = gpiod_request_config_new();

    gpiod_request_config_set_consumer(req_cfg, "bmi088-cs");

    struct gpiod_line_request *req = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

    gpiod_request_config_free(req_cfg);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    gpiod_chip_close(chip);

    if (!req) {
        perror("gpiod_chip_request_lines");
        close(fd);
        return -1;
    }

    accel_ctx->spi_fd = fd;
    accel_ctx->cs_req = req;
    accel_ctx->cs_offset = accel_cs_offset;

    gyro_ctx->spi_fd = fd;
    gyro_ctx->cs_req = req;
    gyro_ctx->cs_offset = gyro_cs_offset;

    return 0;
}

void rpi_bmi08x_platform_deinit(struct rpi_bmi08x_cs *accel_ctx)
{
    if (accel_ctx->cs_req) {
        gpiod_line_request_release(accel_ctx->cs_req);
    }

    if (accel_ctx->spi_fd >= 0) {
        close(accel_ctx->spi_fd);
    }
}

static int rpi_bmi08x_xfer(struct rpi_bmi08x_cs *ctx, uint8_t *tx, uint8_t *rx, uint32_t len)
{
    struct spi_ioc_transfer xfer = {
        .tx_buf = (unsigned long)tx,
        .rx_buf = (unsigned long)rx,
        .len = len,
        .speed_hz = BMI08X_SPI_SPEED_HZ,
        .bits_per_word = 8,
    };

    gpiod_line_request_set_value(ctx->cs_req, ctx->cs_offset, GPIOD_LINE_VALUE_ACTIVE);
    int ret = ioctl(ctx->spi_fd, SPI_IOC_MESSAGE(1), &xfer);
    gpiod_line_request_set_value(ctx->cs_req, ctx->cs_offset, GPIOD_LINE_VALUE_INACTIVE);

    return ret;
}

BMI08_INTF_RET_TYPE rpi_bmi08x_spi_read(uint8_t reg_addr, uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    struct rpi_bmi08x_cs *ctx = (struct rpi_bmi08x_cs *)intf_ptr;
    uint8_t tx[BMI08X_MAX_XFER_LEN + 1] = { 0 };
    uint8_t rx[BMI08X_MAX_XFER_LEN + 1] = { 0 };

    if (len > BMI08X_MAX_XFER_LEN) {
        return -1;
    }

    tx[0] = reg_addr;

    if (rpi_bmi08x_xfer(ctx, tx, rx, len + 1) < 1) {
        return -1;
    }

    memcpy(reg_data, &rx[1], len);

    return BMI08_OK;
}

BMI08_INTF_RET_TYPE rpi_bmi08x_spi_write(uint8_t reg_addr, const uint8_t *reg_data, uint32_t len, void *intf_ptr)
{
    struct rpi_bmi08x_cs *ctx = (struct rpi_bmi08x_cs *)intf_ptr;
    uint8_t tx[BMI08X_MAX_XFER_LEN + 1];

    if (len > BMI08X_MAX_XFER_LEN) {
        return -1;
    }

    tx[0] = reg_addr;
    memcpy(&tx[1], reg_data, len);

    if (rpi_bmi08x_xfer(ctx, tx, NULL, len + 1) < 1) {
        return -1;
    }

    return BMI08_OK;
}

void rpi_bmi08x_delay_us(uint32_t period, void *intf_ptr)
{
    (void)intf_ptr;

    struct timespec ts;

    ts.tv_sec = period / 1000000u;
    ts.tv_nsec = (long)(period % 1000000u) * 1000L;
    nanosleep(&ts, NULL);
}

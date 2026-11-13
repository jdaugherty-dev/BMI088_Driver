/*
 * Minimal BMI088 bring-up test for the Raspberry Pi 4 breakout board.
 * Initializes both dies over SPI, then polls at ~100 Hz, prints accel
 * (m/s^2) and gyro (deg/s) to confirm the wiring/driver works, and
 * optionally streams each sample over UDP to the live viewer
 * (../viewer/imu_viewer.py).
 *
 * Usage: ./bmi088_test [viewer_host] [viewer_port]
 */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "bmi08x.h"
#include "rpi_platform.h"
#include "udp_stream.h"

#define SPI_DEV       "/dev/spidev0.0"
#define GPIOCHIP      "/dev/gpiochip0"
#define ACCEL_CS_GPIO 24u /* CS1, header physical pin 18 */
#define GYRO_CS_GPIO  23u /* CS2, header physical pin 16 */

#define ACCEL_RANGE_G 24.0f  /* BMI088_ACCEL_RANGE_24G */
#define GYRO_RANGE_DPS 250.0f /* BMI08_GYRO_RANGE_250_DPS */

static volatile sig_atomic_t g_run = 1;

static void on_sigint(int sig)
{
    (void)sig;
    g_run = 0;
}

static void check(const char *what, int8_t rslt)
{
    if (rslt != BMI08_OK) {
        fprintf(stderr, "%s failed: %d\n", what, rslt);
        exit(EXIT_FAILURE);
    }
}

static float lsb_to_mps2(int16_t val, float g_range)
{
    return (9.80665f * (float)val * g_range) / 32768.0f;
}

static float lsb_to_dps(int16_t val, float dps_range)
{
    return (dps_range * (float)val) / 32768.0f;
}

static uint64_t monotonic_us(struct timespec start)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);

    return (uint64_t)(now.tv_sec - start.tv_sec) * 1000000u +
           (uint64_t)(now.tv_nsec - start.tv_nsec) / 1000u;
}

int main(int argc, char **argv)
{
    struct bmi08_dev dev = { 0 };
    struct rpi_bmi08x_cs accel_cs, gyro_cs;
    struct udp_stream stream = { 0 };
    struct timespec start;

    const char *viewer_host = (argc > 1) ? argv[1] : NULL;
    uint16_t viewer_port = (argc > 2) ? (uint16_t)atoi(argv[2]) : 5005;

    if (viewer_host && udp_stream_init(&stream, viewer_host, viewer_port) == 0) {
        printf("streaming to %s:%u\n", viewer_host, viewer_port);
    }

    signal(SIGINT, on_sigint);
    clock_gettime(CLOCK_MONOTONIC, &start);

    if (rpi_bmi08x_platform_init(&accel_cs, &gyro_cs, SPI_DEV, GPIOCHIP,
                                  ACCEL_CS_GPIO, GYRO_CS_GPIO) != 0) {
        fprintf(stderr, "platform init failed\n");
        return EXIT_FAILURE;
    }

    dev.intf = BMI08_SPI_INTF;
    dev.variant = BMI088_VARIANT;
    dev.read = rpi_bmi08x_spi_read;
    dev.write = rpi_bmi08x_spi_write;
    dev.delay_us = rpi_bmi08x_delay_us;
    dev.read_write_len = 32;
    dev.intf_ptr_accel = &accel_cs;
    dev.intf_ptr_gyro = &gyro_cs;

    check("bmi08xa_init", bmi08xa_init(&dev));
    check("bmi08g_init", bmi08g_init(&dev));

    printf("accel chip id: 0x%02X, gyro chip id: 0x%02X\n", dev.accel_chip_id, dev.gyro_chip_id);

    check("bmi08a_load_config_file", bmi08a_load_config_file(&dev));

    dev.accel_cfg.odr = BMI08_ACCEL_ODR_100_HZ;
    dev.accel_cfg.range = BMI088_ACCEL_RANGE_24G;
    dev.accel_cfg.power = BMI08_ACCEL_PM_ACTIVE;
    dev.accel_cfg.bw = BMI08_ACCEL_BW_NORMAL;
    check("bmi08a_set_power_mode", bmi08a_set_power_mode(&dev));
    check("bmi08xa_set_meas_conf", bmi08xa_set_meas_conf(&dev));

    dev.gyro_cfg.odr = BMI08_GYRO_BW_32_ODR_100_HZ;
    dev.gyro_cfg.bw = BMI08_GYRO_BW_32_ODR_100_HZ;
    dev.gyro_cfg.range = BMI08_GYRO_RANGE_250_DPS;
    dev.gyro_cfg.power = BMI08_GYRO_PM_NORMAL;
    check("bmi08g_set_power_mode", bmi08g_set_power_mode(&dev));
    check("bmi08g_set_meas_conf", bmi08g_set_meas_conf(&dev));

    printf("sample_count, ax_mps2, ay_mps2, az_mps2, gx_dps, gy_dps, gz_dps\n");

    struct bmi08_sensor_data accel = { 0 };
    struct bmi08_sensor_data gyro = { 0 };
    uint32_t count = 0;

    while (g_run) {
        uint8_t status = 0;

        if (bmi08a_get_data_int_status(&status, &dev) == BMI08_OK &&
            (status & BMI08_ACCEL_DATA_READY_INT)) {
            bmi08a_get_data(&accel, &dev);
        }

        if (bmi08g_get_data_int_status(&status, &dev) == BMI08_OK &&
            (status & BMI08_GYRO_DATA_READY_INT)) {
            bmi08g_get_data(&gyro, &dev);
        }

        float ax = lsb_to_mps2(accel.x, ACCEL_RANGE_G);
        float ay = lsb_to_mps2(accel.y, ACCEL_RANGE_G);
        float az = lsb_to_mps2(accel.z, ACCEL_RANGE_G);
        float gx = lsb_to_dps(gyro.x, GYRO_RANGE_DPS);
        float gy = lsb_to_dps(gyro.y, GYRO_RANGE_DPS);
        float gz = lsb_to_dps(gyro.z, GYRO_RANGE_DPS);

        printf("%u, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f\n", count, ax, ay, az, gx, gy, gz);

        struct imu_udp_packet pkt = {
            .seq = count,
            .t_us = monotonic_us(start),
            .ax = ax, .ay = ay, .az = az,
            .gx = gx, .gy = gy, .gz = gz,
        };
        udp_stream_send(&stream, &pkt);

        count++;
        rpi_bmi08x_delay_us(10000, NULL); /* ~100 Hz sample rate */
    }

    udp_stream_close(&stream);
    rpi_bmi08x_platform_deinit(&accel_cs);

    return EXIT_SUCCESS;
}

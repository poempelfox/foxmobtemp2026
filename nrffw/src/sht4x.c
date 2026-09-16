/* Talking to SHT4x (SHT40, SHT41, SHT45) temperature / humidity sensors */

#include <zephyr/drivers/i2c.h>
#include "sht4x.h"

#define SHT4XBASEADDR 0x44 /* there are variants with 0x45 and 0x46, but 0x44 is by far the most common. */

/* Measurement with high precision */
#define SHT4X_CMD_MEASURE_HIGH 0xFD
/* Turn on heater with medium power (110 mW) for 1 second */
#define SHT4X_CMD_HEAT_MID_LONG 0x2F

static const struct i2c_dt_spec sht4xi2cdev = I2C_DT_SPEC_GET(DT_NODELABEL(sht45));

void sht4x_init(void)
{
    /* The default power-on-config of the sensor should
     * be perfectly fine for us, so there is nothing to
     * configure here. */
}

void sht4x_startmeas(void)
{
    uint8_t cmd[1] = { SHT4X_CMD_MEASURE_HIGH };
    i2c_write_dt(&sht4xi2cdev, cmd, sizeof(cmd));
    /* We ignore the return value. If that failed, we'll notice
     * soon enough, namely when we try to read the result... */
}

/* This function is based on Sensirons example code and datasheet
 * for the SHT3x and was written for that. CRC-calculation is
 * exactly the same for the SHT4x, so we reuse it. */
static uint8_t sht4x_crc(uint8_t b1, uint8_t b2)
{
    uint8_t crc = 0xff; /* Start value */
    uint8_t b;
    crc ^= b1;
    for (b = 0; b < 8; b++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x131;
      } else {
        crc = crc << 1;
      }
    }
    crc ^= b2;
    for (b = 0; b < 8; b++) {
      if (crc & 0x80) {
        crc = (crc << 1) ^ 0x131;
      } else {
        crc = crc << 1;
      }
    }
    return crc;
}

void sht4x_read(struct sht4xdata * d)
{
    uint8_t readbuf[6];
    d->valid = 0; d->tempraw = 0xffff;  d->humraw = 0xffff;
    d->temp = -999.99; d->hum = 200.0;
    int res = i2c_read_dt(&sht4xi2cdev, readbuf, sizeof(readbuf));
    if (res != 0) {
      printk("[sht4x.c] ERROR: I2C-read from SHT4x failed.\n");
      return;
    }
    /* Check CRC */
    if (sht4x_crc(readbuf[0], readbuf[1]) != readbuf[2]) {
      printk("[sht4x.c] ERROR: CRC-check for read part 1 failed.\n");
      return;
    }
    if (sht4x_crc(readbuf[3], readbuf[4]) != readbuf[5]) {
      printk("[sht4x.c] ERROR: CRC-check for read part 2 failed.\n");
      return;
    }
    /* OK, CRC matches, this is looking good. */
    d->tempraw = (readbuf[0] << 8) | readbuf[1];
    d->humraw = (readbuf[3] << 8) | readbuf[4];
    d->temp = -45.0 + 175.0 * ((double)d->tempraw / 65535.0);
    d->hum = -6.0 + 125.0 * ((double)d->humraw / 65535.0);
    /* Cap values to 0-100 range - the sensor may return values
     * that are slightly outside that range */
    if (d->hum < 0.0) { d->hum = 0.0; }
    if (d->hum > 100.0) { d->hum = 100.0; }
    /* Mark the result as valid. */
    d->valid = 1;
}

void sht4x_heatercycle(void)
{
    uint8_t cmd[1] = { SHT4X_CMD_HEAT_MID_LONG };
    printk("[sht4x.c] turning SHT4x heater on for 1.0 seconds at medium power (110 mW).\n");
    i2c_write_dt(&sht4xi2cdev, cmd, sizeof(cmd));
}


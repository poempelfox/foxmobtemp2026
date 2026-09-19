/* Talking to dfrobot lark weather station over I2C */

#define TWIM_FREQUENCY_FREQUENCY_K100 (0x0028F000UL)

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <math.h>
#include <stdlib.h>
#include "larkws.h"

/* #define LARKWSBASEADDR 0x21  - 0x21 to 0x23 supported?
 * not needed here, it's defined in the device tree. */

/* raw commands sent to the weatherstation */
#define LWS_GET_DATA      0x00
#define LWS_GET_ALL_DATA  0x01
#define LWS_SET_TIME      0x02
#define LWS_GET_TIME      0x03
#define LWS_GET_UNIT      0x04
#define LWS_GET_VERSION   0x05

#define LWS_STATUS_SUCCESS  0x53

static const struct i2c_dt_spec larkwsi2cdev = I2C_DT_SPEC_GET(DT_NODELABEL(larkws));

void larkws_init(void)
{
    /* Nothing to do here really. */
}

void larkws_getvalue_string(const char * what, char * result, int reslen)
{
    char i2cpacket[16];
    int ret;
    result[0] = 0;
    if (reslen < 6) {
      return; /* Not supported. Just fail. */
    }
    if (strlen(what) > 8) {
      return;
    }
    i2cpacket[0] = LWS_GET_DATA;
    i2cpacket[1] = strlen(what) & 0xff; /* low byte */
    i2cpacket[2] = 0; /* high byte - always 0 anyways due to length constraints */
    strcpy(&i2cpacket[3], what);
    ret = i2c_write_dt(&larkwsi2cdev, &i2cpacket[0], strlen(what) + 3);
    if (ret != 0) {
      printk("[larkws] I2C write to weather station failed.\n");
      return;
    }
    uint32_t stts = k_uptime_get_32();
    do {
      ret = i2c_read_dt(&larkwsi2cdev, &i2cpacket[0], 1);
      if (ret == 0) { break; }
      /* Retry until timeout */
      printk(".");
    } while ((k_uptime_get_32() - stts) < 500U);
    if (ret != 0) { /* We never got a result. */
      printk("[larkws] I2C read timed out.\n");
      return;
    }
    if (i2cpacket[0] != LWS_STATUS_SUCCESS) {
      /* the result was not 'success' */
      printk("[larkws] Weather station returned %x instead of success.\n",
             i2cpacket[0]);
      return;
    }
    /* Now it gets interesting: There seems to be a valid
     * result. Read it. */
    ret = i2c_read_dt(&larkwsi2cdev, &i2cpacket[0], 3);
    if (ret != 0) { /* ?! */
      printk("[larkws] 2nd I2C read failed?!\n");
      return;
    }
    if (i2cpacket[0] != LWS_GET_DATA) {
      printk("[larkws] got result for wrong command?!\n");
      return;
    }
    unsigned int len = i2cpacket[1] | (i2cpacket[2] << 8);
    if (len > sizeof(i2cpacket)) {
      printk("[larkws] got oversized reply %u bytes %x %x\n",
             len, i2cpacket[1], i2cpacket[2]);
      return;
    }
    ret = i2c_read_dt(&larkwsi2cdev, &i2cpacket[0], len);
    if (ret != 0) { /* ?! */
      printk("[larkws] 3rd I2C read failed?!\n");
      return;
    }
    /* Copy result into output buffer */
    int i;
    for (i = 0; ((i < (reslen - 1)) && (i < len)); i++) {
      result[i] = i2cpacket[i];
    }
    result[i] = 0;
}

double larkws_getvalue_double(const char * what)
{
    char buf[16]; double res; char * endptr;
    larkws_getvalue_string(what, &buf[0], sizeof(buf));
    if (buf[0] == 0) { /* no valid reply */
      printk("[larkws] nothing to convert to double\n");
      return NAN;
    }
    res = strtod(buf, &endptr);
    if (strlen(endptr) != 0) {
      printk("[larkws] characters remaining after double conversion\n");
      return NAN;
    }
    return res;
}


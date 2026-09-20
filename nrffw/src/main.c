/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <modem/nrf_modem_lib.h>
#include <modem/location.h>
#include <dk_buttons_and_leds.h>
#include "larkws.h"
#include "mobilenet.h"


static K_SEM_DEFINE(location_event, 0, 1);

volatile int gps_gotafix = 0;
double gps_lat, gps_lon, gps_acc;

static void location_event_handler(const struct location_event_data *event_data)
{
    switch (event_data->id) {
    case LOCATION_EVT_LOCATION:
            printk("Got location:\n");
            printk("  method: %s\n", location_method_str(event_data->method));
            printk("  latitude: %.06f\n", event_data->location.latitude);
            printk("  longitude: %.06f\n", event_data->location.longitude);
            printk("  accuracy: %.01f m\n", (double)event_data->location.accuracy);
            gps_gotafix = 1;
            gps_lat = event_data->location.latitude;
            gps_lon = event_data->location.longitude;
            gps_acc = event_data->location.accuracy;
            if (event_data->location.datetime.valid) {
                    printk("  date: %04d-%02d-%02d\n",
                            event_data->location.datetime.year,
                            event_data->location.datetime.month,
                            event_data->location.datetime.day);
                    printk("  time: %02d:%02d:%02d.%03d UTC\n",
                            event_data->location.datetime.hour,
                            event_data->location.datetime.minute,
                            event_data->location.datetime.second,
                            event_data->location.datetime.ms);
            }
            k_sem_give(&location_event);
            break;
    case LOCATION_EVT_TIMEOUT:
    case LOCATION_EVT_ERROR:
            printk("Location event: %s\n", ((event_data->id == LOCATION_EVT_TIMEOUT) ? "timeout" : "ERROR"));
            /* No need to wait any longer, we will not get a fix ever. */
            k_sem_give(&location_event);
            break;
    default:
            printk("Getting location: Unknown event id %d\n", event_data->id);
            break;
    }
}

int main(void)
{
    int err;

    printk("Fox mobile temp 2026 iteration firmware started\n\r");

    /* Init the mini library for the onboard buttons and leds */
    dk_leds_init();
    //dk_buttons_init(HANDLER);

    /* Init the modem but do not connect to the network yet.
     * the GPS shares the antenna, so we must not connect before we are
     * done with the GPS, or else we're never going to get a fix as
     * we won't get the shared antenna for 4 minutes in a row. */
    mobilenet_init();

    printk("Trying to find GPS location (expected 4 min, timeout 10 min)...\n");
    err = location_init(location_event_handler);
    if (err) {
      printk("Initializing the Location library failed, error: %d\n", err);
      return 0;
    }
    struct location_config lc;
    enum location_method methods[] = { LOCATION_METHOD_GNSS };
    location_config_defaults_set(&lc, ARRAY_SIZE(methods), methods);
    lc.interval = 0;
    lc.methods[0].gnss.timeout = 600 * MSEC_PER_SEC;
    lc.methods[0].gnss.priority_mode = false;
    lc.methods[0].gnss.visibility_detection = true;
    location_request(&lc);
    /* Wait for a fix */
    int secstowait = 600;
    int ledstate = 0;
    int gotsem;
    do {
      gotsem = k_sem_take(&location_event, K_SECONDS(1));
      /* We blink LED3 while we wait */
      ledstate = !ledstate;
      if (ledstate == 0) {
        dk_set_led_off(DK_LED3);
      } else {
        dk_set_led_on(DK_LED3);
      }
      secstowait--;
    } while ((gotsem != 0) && (secstowait > 0));
    location_request_cancel(); /* try to cancel anything that is still pending */
    if (gps_gotafix) {
      /* Turn LED3 on permanently to signal success */
      dk_set_led_on(DK_LED3);
    } else {
      /* Turn LED3 off permanently to signal failure */
      dk_set_led_off(DK_LED3);
    }

    /* Initialize sensor */
    larkws_init();
    /* connect to mobile network */
    mobilenet_connect();

    /* send location info - if any */
    if (gps_gotafix) {
      mobilenet_sendlocation(gps_lat, gps_lon);
    }

    while (1) {
      /* Loop around this. Do not turn off LTE - the module should
       * use way less power if it does not have to reconnect all
       * the time. */
      k_sleep(K_SECONDS(180));
      double temp = larkws_getvalue_double(LARKWS_TEMPERATURE);
      double hum = larkws_getvalue_double(LARKWS_HUMIDITY);
      double press = larkws_getvalue_double(LARKWS_PRESSURE);
      double windsp = larkws_getvalue_double(LARKWS_WINDSPEED);
      /* we convert the wind speed from m/s to km/h. Our other stations all use
       * that as unit, and I find it a lot easier to visualize such a value. */
      windsp = (windsp * 3600.0) / 1000.0;
      char winddir[16];
      larkws_getvalue_string(LARKWS_WINDDIR, winddir, sizeof(winddir));
      printk("Temperature: %.2lf (degC)\n", temp);
      printk("Humidity:    %.2lf (%%)\n", hum);
      printk("Pressure:    %.2lf (hPa | mbar)\n", press);
      printk("WindSpeed:   %.2lf (km/h)\n", windsp);
      printk("WindDir:     %s\n", winddir);
      int res = mobilenet_sendweatherdata(temp, hum, press, windsp, winddir);
      if (res == 1) {
        printk("Sending weather data succeeded.\n");
      } else {
        printk("Attempt to send weather data failed.\n");
      }
    }

    /* Never reached. */
    /* mobilenet_shutdown(); */
    return 0;
}


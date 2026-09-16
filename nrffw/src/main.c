/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <modem/modem_key_mgmt.h>
#include <modem/location.h>
#include <dk_buttons_and_leds.h>
#include <zephyr/drivers/i2c.h>
#include "sht4x.h"

#define HTTP_PORT 80
#define HTTP_HOSTNAME "wetter.poempelfox.de"
#define HTTP_PATH "/does-not-exist-hi-this-is-your-nrf9151dk"

#define REQUEST "GET " HTTP_PATH " HTTP/1.1\r\nHost: " HTTP_HOSTNAME "\r\n\r\n"

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

int trysubmitdata(void)
{
	int err;
	int fd = -1;
	int succ = 0;
	struct addrinfo * reshn = NULL;
	char response[1024];

	printk("Waiting for network... ");
	err = lte_lc_connect();
	if (err) {
		printk("Failed to connect to the LTE network, err %d\n", err);
		goto cleanupandout;
	}
	printk("OK\n");

	struct addrinfo hints = {
		.ai_family = AF_INET, /* Our IOT SIM only does IPv4 anyways :( */
		.ai_socktype = SOCK_STREAM,
	};
	printk("Resolving hostname %s... ", HTTP_HOSTNAME);
	err = zsock_getaddrinfo(HTTP_HOSTNAME, NULL, &hints, &reshn);
	if (err) {
		printk("getaddrinfo() failed, err %d, %s\n", errno, strerror(errno));
		goto cleanupandout;
	}
	char ipv4txt[NET_IPV4_ADDR_LEN];
	zsock_inet_ntop(AF_INET, &(((struct sockaddr_in *)reshn->ai_addr)->sin_addr.s_addr), ipv4txt, sizeof(ipv4txt));
	printk("Resolved to %s\n", ipv4txt);
	((struct sockaddr_in *)reshn->ai_addr)->sin_port = htons(HTTP_PORT);

	printk("Opening socket... ");
	fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (fd == -1) {
		printk("Failed to open socket!\n");
		goto cleanupandout;
	}

	printk("Connecting to %s on port %d... ", HTTP_HOSTNAME, HTTP_PORT);
	err = zsock_connect(fd, reshn->ai_addr, sizeof(struct sockaddr_in));
	if (err) {
		printk("connect() failed, err: %d, %s\n", errno, strerror(errno));
		goto cleanupandout;
	} else {
		printk("Connected.\n");
	}

	printk("Sending data... ");
	err = zsock_send(fd, REQUEST, sizeof(REQUEST)-1, 0);
	if (err < 0) {
		printk("send() failed, err: %d, %s\n", errno, strerror(errno));
	}

	printk("Response:\n\n");
	while (1) {
		int len = zsock_recv(fd, response, sizeof(response) - 1, 0);

		if (len < 0) {
			printk("Error reading response\n");
			return 0;
		}

		if (len == 0) {
			break;
		}

		response[len] = 0;
		printk("%s", response);
	}
	
	/* FIXME: we should actually check if we got an OK from the server, not just any reply. */
	succ = 1;

cleanupandout:
	if (fd >= 0) {
		zsock_close(fd);
	}
	if (reshn != NULL) {
		zsock_freeaddrinfo(reshn);
	}
	lte_lc_power_off();
	printk("\ncleanup finished.\n");

	return succ;
}

int main(void)
{
	int err;

	printk("Fox mobile temp 2026 iteration firmware started\n\r");

	/* Init the mini library for the onboard buttons and leds */
	dk_leds_init();
	//dk_buttons_init(HANDLER);

	err = nrf_modem_lib_init();
	if (err) {
		printk("Modem library initialization failed, error: %d\n", err);
		return 0;
	}

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
		/* TODO send location info */
		/* Turn LED3 on permanently to signal success */
		dk_set_led_on(DK_LED3);
	} else {
		/* Turn LED3 off permanently to signal failure */
		dk_set_led_off(DK_LED3);
	}

	/* Initialize sensor */
	sht4x_init();
	sht4x_startmeas();

	/* slightly more than 1 second should be plenty
	 * for the SHT4x to finish measurement - datasheet say 8.3ms max. */
	k_sleep(K_MSEC(1111));
	struct sht4xdata temphumdata;
	sht4x_read(&temphumdata);
	printk("Read SHT4x sensor: valid %d; temp %.2f; hum %.2f%%\n",
	       temphumdata.valid, temphumdata.temp, temphumdata.hum);

	/* Turn on LTE */

	/* Loop around this. Do not turn off LTE - the module should
	 * use way less power if it does not have to reconnect all
	 * the time. */
	trysubmitdata();

	/* Never reached. */
	return 0;
}


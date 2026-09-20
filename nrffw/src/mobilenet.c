
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <modem/modem_key_mgmt.h>
#include <dk_buttons_and_leds.h>
#include <math.h>
/* This defines WPDTOKEN: */
#include "secrets.h"

/* the sensor IDs used in submissions to wetter.poempelfox.de. */
#define SENSORID_TEMP "113"
#define SENSORID_HUM "112"
#define SENSORID_PRESS "114"
#define SENSORID_WINDSPEED "115"
#define SENSORID_WINDDIR "116"

#define HTTP_HOSTNAME_LOC "wetter.poempelfox.de"
#define HTTP_PORT_LOC 80
#define HTTP_PATH_LOC "/doesnotexist/submit-location/?"

#define HTTP_HOSTNAME_WEDA "wetter.poempelfox.de"
#define HTTP_PORT_WEDA 80
#define HTTP_PATH_WEDA "/api/pushmeasurement/"

#define REQUEST_LOC1 "GET " HTTP_PATH_LOC
#define REQUEST_LOC2 " HTTP/1.1\r\nHost: " HTTP_HOSTNAME_LOC "\r\nConnection: close\r\n\r\n"

#define REQUEST_WEDA1 "POST " HTTP_PATH_WEDA " HTTP/1.1\r\nHost: " HTTP_HOSTNAME_WEDA "\r\nConnection: close\r\nContent-type: application/json\r\nX-Sensor: "
#define REQUEST_WEDA2 "\r\nContent-Length: "
#define REQUEST_WEDA3 "\r\n\r\n"

void mobilenet_init(void)
{
  int err = nrf_modem_lib_init();
  if (err) {
    printk("Modem library initialization failed, error: %d\n", err);
    return;
  }
}

void mobilenet_connect(void)
{
  printk("Waiting for network... ");
  int err = lte_lc_connect();
  if (err) {
    printk("Failed to connect to the LTE network, err %d\n", err);
    return;
  }
  printk("OK\n");
}

void mobilenet_shutdown(void)
{
  lte_lc_power_off();
}

int mobilenet_sendhttprequest(const char * host, uint16_t port, const char * request)
{
  int err;
  int fd = -1;
  int succ = 0;
  struct addrinfo * reshn = NULL;

  struct addrinfo hints = {
      .ai_family = AF_INET, /* Our IOT SIM only does IPv4 anyways :( */
      .ai_socktype = SOCK_STREAM,
  };
  printk("Resolving hostname %s... ", host);
  err = zsock_getaddrinfo(host, NULL, &hints, &reshn);
  if (err) {
    printk("getaddrinfo() failed, err %d, %s\n", errno, strerror(errno));
    goto cleanupandout;
  }
  char ipv4txt[NET_IPV4_ADDR_LEN];
  zsock_inet_ntop(AF_INET, &(((struct sockaddr_in *)reshn->ai_addr)->sin_addr.s_addr), ipv4txt, sizeof(ipv4txt));
  printk("Resolved to %s\n", ipv4txt);
  ((struct sockaddr_in *)reshn->ai_addr)->sin_port = htons(port);

  printk("Opening socket... ");
  fd = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (fd == -1) {
    printk("Failed to open socket!\n");
    goto cleanupandout;
  }

  printk("Connecting to %s on port %d... ", host, port);
  err = zsock_connect(fd, reshn->ai_addr, sizeof(struct sockaddr_in));
  if (err) {
    printk("connect() failed, err: %d, %s\n", errno, strerror(errno));
    goto cleanupandout;
  } else {
    printk("Connected.\n");
  }

  printk("Sending data... ");
  err = zsock_send(fd, request, strlen(request), 0);
  if (err < 0) {
    printk("send() failed, err: %d, %s\n", errno, strerror(errno));
  }

  printk("Response:\n\n");
  char rbuf[2];
  char lastline[300];
  strcpy(lastline, "");
  rbuf[1] = 0;
  while (1) {
    /* Read the reply byte by byte, assembling lines that we can then parse. */
    int len = zsock_recv(fd, rbuf, 1, 0);
    if (len < 0) {
      printk("Error reading response\n");
      break;
    }
    if (len == 0) { /* We have reached the end */
      break;
    }
    /* Append to lastline */
    if ((rbuf[0] == '\r') || (rbuf[0] == '\n')) {
      printk("Processing line: '%s'\n", lastline);
      /* process the now completed line. */
      if (strncmp(lastline, "HTTP/", 5) == 0) {
        int l = strlen(lastline);
        int i = 5;
        while ((i < (l - 1)) && (lastline[i] != ' ')) { i++; }
        if (strncmp(&lastline[i], " 200", 4) == 0) {
          /* we found HTTP STATUS 200! */
          succ = 1;
        }
      }
      strcpy(lastline, "");
    } else {
      if (strlen(lastline) < (sizeof(lastline) - 2)) {
        strcat(lastline, rbuf);
      }
    }
  }

cleanupandout:
  if (fd >= 0) {
    zsock_close(fd);
  }
  if (reshn != NULL) {
    zsock_freeaddrinfo(reshn);
  }
  printk("\ncleanup finished.\n");

  return succ;
}

int mobilenet_sendlocation(double lat, double lon)
{
  char request[sizeof(REQUEST_LOC1) + sizeof(REQUEST_LOC2) + 100];
  strcpy(request, REQUEST_LOC1);
  sprintf(&request[strlen(request)], "lat=%.4lf&lon=%.4lf", lat, lon);
  strcat(request, REQUEST_LOC2);
  int res = mobilenet_sendhttprequest(HTTP_HOSTNAME_LOC, HTTP_PORT_LOC, request);
  return res;
}

struct widimapentry {
  char * str;
  double deg;
};

/* According to the documentation this only has 8 directions */
static const struct widimapentry widimap[] = {
  { .str = "N",   .deg =   0.0 },
  { .str = "NE",  .deg =  45.0 },
  { .str = "E",   .deg =  90.0 },
  { .str = "SE",  .deg = 135.0 },
  { .str = "S",   .deg = 180.0 },
  { .str = "SW",  .deg = 225.0 },
  { .str = "W",   .deg = 270.0 },
  { .str = "NW",  .deg = 315.0 }
};

int mobilenet_sendweatherdata(double temp, double hum, double press, double windspeed, char * winddir)
{
  char request[sizeof(REQUEST_WEDA1) + sizeof(REQUEST_WEDA2) + sizeof(REQUEST_WEDA3) + 500];
  char rcont[400];
  int nrvals = 0;
  if (strlen(WPDTOKEN) < 10) {
    printk("[mobilenet] NOT submitting weather data because WPDTOKEN is not set.\n");
    return 0;
  }
  /* convert wind direction to a float/double */
  double fwidir = NAN;
  for (int i = 0; i < 8; i++) {
    if (strcmp(winddir, widimap[i].str) == 0) {
      fwidir = widimap[i].deg;
    }
  }
  strcpy(rcont, "{\"software_version\":\"foxmobtemp2026v0.1\",\"sensordatavalues\":[");
  /* Helper macro to avoid copy+paste orgy */
#define ADDTOSUBM(what, sensorid, formatstring) \
  if (!isnan(what)) { \
    if (nrvals > 0) { strcat(rcont, ","); } \
    sprintf(&rcont[strlen(rcont)], \
            "{\"value_type\":\"%s\",\"value\":\"" formatstring "\"}", sensorid, what); \
    nrvals++; \
  }
  /* End of macro definition */
  ADDTOSUBM(temp, SENSORID_TEMP, "%.2f");
  ADDTOSUBM(hum, SENSORID_HUM, "%.2f");
  ADDTOSUBM(press, SENSORID_PRESS, "%.3f");
  ADDTOSUBM(windspeed, SENSORID_WINDSPEED, "%.3f");
  ADDTOSUBM(fwidir, SENSORID_WINDDIR, "%.1f");
  /* Clean up the macro */
#undef ADDTOSUBM
  if (nrvals <= 0) {
    printk("[mobilenet] NOT submitting weather data because there is no valid data to send.\n");
    return 0;
  }
  strcat(rcont, "]}\n");
  strcpy(request, REQUEST_WEDA1);
  strcat(request, WPDTOKEN);
  strcat(request, REQUEST_WEDA2);
  sprintf(&request[strlen(request)], "%u", strlen(rcont));
  strcat(request, REQUEST_WEDA3);
  strcat(request, rcont);
  printk("Attempting to send request:\n%s\n", request);
  int res = mobilenet_sendhttprequest(HTTP_HOSTNAME_WEDA, HTTP_PORT_WEDA, request);
  return res;
}


/* Functions for the mobile network */

#ifndef _MOBILENET_H_
#define _MOBILENET_H_

/* Initialize the modem */
void mobilenet_init(void);

/* connect to the network. */
void mobilenet_connect(void);

/* shut down the mobile network */
void mobilenet_shutdown(void);

/* Returns 1 on success */
int mobilenet_sendlocation(double lat, double lon);

/* Returns 1 on success */
int mobilenet_sendweatherdata(double temp, double hum, double press, double windspeed, char * winddir);

#endif /* _MOBILENET_H_ */

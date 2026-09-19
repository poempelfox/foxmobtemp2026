
/* Talking to dfrobot lark weatherstation over I2C */

#ifndef _LARKWS_H_
#define _LARKWS_H_

#define LARKWS_TEMPERATURE "Temp"
#define LARKWS_HUMIDITY "Humi"
#define LARKWS_WINDSPEED "Speed"
#define LARKWS_WINDDIR "Dir"
#define LARKWS_PRESSURE "Pressure"

/* Initialize the Lark Weather Station */
void larkws_init(void);

/* Request one type of data from the weather station.
 * Parameters:
 * what - which sensor value to get. Irritatingly, due to the weather stations
 *    interface, this is a string, e.g. "Pressure". There are defines that
 *    can be used, like e.g. LARKWS_TEMPERATURE
 * result - where to write the result. This needs to be at least 6 bytes large.
 * reslen - size of the result buffer.
 */
void larkws_getvalue_string(const char * what, char * result, int reslen);

/* Like above, but returns a float, or "NaN" on error. */
double larkws_getvalue_double(const char * what);

#endif /* _LARKWS_H_ */


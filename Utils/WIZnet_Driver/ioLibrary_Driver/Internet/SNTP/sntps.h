/*
 * sntp.h
 *
 *  Created on: 2014. 12. 15.
 *      Author: Administrator
 */

#ifndef SNTPS_H_
#define SNTPS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/*
 * @brief Define it for Debug & Monitor DNS processing.
 * @note If defined, it dependens on <stdio.h>
 */
//#define _SNTP_DEBUG_

#define	MAX_SNTP_BUF_SIZE	sizeof(ntpsformat)		///< maximum size of DNS buffer. */

/* for ntpclient */
typedef signed char s_char;
typedef unsigned long long tstamp;
typedef unsigned int tdist;

typedef struct
{
  uint32_t second;
  uint32_t fraction;
} ntp_timestamp; // 64 bit

typedef struct _ntpsformat
{
  unsigned char leapVersionMode;
#if 0
  unsigned leap : 2; /* leap indicator */
  unsigned version : 3; /* version number */
  unsigned mode : 3; /* mode */
#endif
	char stratum;
  char poll;
  char precision;
  uint32_t rootDelay;
  uint32_t rootDispersion;
  char referenceId[4];
  uint32_t referenceTimestampSeconds;
  uint32_t referenceTimestampFraction;

  uint32_t originTimestampSeconds;
  uint32_t originTimestampFraction;

  uint32_t receiveTimestampSeconds;
  uint32_t receiveTimestampFraction;
  
  uint32_t transmitTimestampSeconds;
  uint32_t transmitTimestampFraction;
} ntpsformat;

typedef struct _datetime
{
	uint16_t yy;
	uint8_t mo;
	uint8_t dd;
	uint8_t hh;
	uint8_t mm;
	uint8_t ss;
} datetime;

#define ntp_port		123                     //ntp server port number
#define EPOCH 2208988800ull

#define DAYS_IN_YEAR 365
#define HOURS_IN_DAY 24
#define SECONDS_IN_MINUTE 60
#define MINUTES_IN_HOUR 60
#define EPOCH_YEAR 1900
#define LEAP_SECOND_YEAR 1972

void SNTPs_init(uint8_t s, uint8_t *buf);
int8_t SNTPs_run();
uint32_t numberOfSecondsSince1900Epoch();

#ifdef __cplusplus
}
#endif

#endif /* SNTP_H_ */

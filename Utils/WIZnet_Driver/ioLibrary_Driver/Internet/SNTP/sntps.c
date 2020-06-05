/*
 * sntp.c
 *
 *  Created on: 2014. 12. 15.
 *      Author: Administrator
 */


#include <string.h>

#include "sntps.h"
#include "../../Ethernet/socket.h"

ntpsformat NTPsformat;
datetime Nowdatetime;
uint8_t ntpsmessage[48];
uint8_t *data_buf;
uint8_t NTPs_SOCKET;
uint8_t time_zone;
uint16_t ntp_retry_cnt=0; //counting the ntp retry number

/*
00)UTC-12:00 Baker Island, Howland Island (both uninhabited)
01) UTC-11:00 American Samoa, Samoa
02) UTC-10:00 (Summer)French Polynesia (most), United States (Aleutian Islands, Hawaii)
03) UTC-09:30 Marquesas Islands
04) UTC-09:00 Gambier Islands;(Summer)United States (most of Alaska)
05) UTC-08:00 (Summer)Canada (most of British Columbia), Mexico (Baja California)
06) UTC-08:00 United States (California, most of Nevada, most of Oregon, Washington (state))
07) UTC-07:00 Mexico (Sonora), United States (Arizona); (Summer)Canada (Alberta)
08) UTC-07:00 Mexico (Chihuahua), United States (Colorado)
09) UTC-06:00 Costa Rica, El Salvador, Ecuador (Galapagos Islands), Guatemala, Honduras
10) UTC-06:00 Mexico (most), Nicaragua;(Summer)Canada (Manitoba, Saskatchewan), United States (Illinois, most of Texas)
11) UTC-05:00 Colombia, Cuba, Ecuador (continental), Haiti, Jamaica, Panama, Peru
12) UTC-05:00 (Summer)Canada (most of Ontario, most of Quebec)
13) UTC-05:00 United States (most of Florida, Georgia, Massachusetts, most of Michigan, New York, North Carolina, Ohio, Washington D.C.)
14) UTC-04:30 Venezuela
15) UTC-04:00 Bolivia, Brazil (Amazonas), Chile (continental), Dominican Republic, Canada (Nova Scotia), Paraguay,
16) UTC-04:00 Puerto Rico, Trinidad and Tobago
17) UTC-03:30 Canada (Newfoundland)
18) UTC-03:00 Argentina; (Summer) Brazil (Brasilia, Rio de Janeiro, Sao Paulo), most of Greenland, Uruguay
19) UTC-02:00 Brazil (Fernando de Noronha), South Georgia and the South Sandwich Islands
20) UTC-01:00 Portugal (Azores), Cape Verde
21) UTC&#177;00:00 Cote d'Ivoire, Faroe Islands, Ghana, Iceland, Senegal; (Summer) Ireland, Portugal (continental and Madeira)
22) UTC&#177;00:00 Spain (Canary Islands), Morocco, United Kingdom
23) UTC+01:00 Angola, Cameroon, Nigeria, Tunisia; (Summer)Albania, Algeria, Austria, Belgium, Bosnia and Herzegovina,
24) UTC+01:00 Spain (continental), Croatia, Czech Republic, Denmark, Germany, Hungary, Italy, Kinshasa, Kosovo,
25) UTC+01:00 Macedonia, France (metropolitan), the Netherlands, Norway, Poland, Serbia, Slovakia, Slovenia, Sweden, Switzerland
26) UTC+02:00 Libya, Egypt, Malawi, Mozambique, South Africa, Zambia, Zimbabwe, (Summer)Bulgaria, Cyprus, Estonia,
27) UTC+02:00 Finland, Greece, Israel, Jordan, Latvia, Lebanon, Lithuania, Moldova, Palestine, Romania, Syria, Turkey, Ukraine
28) UTC+03:00 Belarus, Djibouti, Eritrea, Ethiopia, Iraq, Kenya, Madagascar, Russia (Kaliningrad Oblast), Saudi Arabia,
29) UTC+03:00 South Sudan, Sudan, Somalia, South Sudan, Tanzania, Uganda, Yemen
30) UTC+03:30 (Summer)Iran
31) UTC+04:00 Armenia, Azerbaijan, Georgia, Mauritius, Oman, Russia (European), Seychelles, United Arab Emirates
32) UTC+04:30 Afghanistan
33) UTC+05:00 Kazakhstan (West), Maldives, Pakistan, Uzbekistan
34) UTC+05:30 India, Sri Lanka
35) UTC+05:45 Nepal
36) UTC+06:00 Kazakhstan (most), Bangladesh, Russia (Ural: Sverdlovsk Oblast, Chelyabinsk Oblast)
37) UTC+06:30 Cocos Islands, Myanmar
38) UTC+07:00 Jakarta, Russia (Novosibirsk Oblast), Thailand, Vietnam
39) UTC+08:00 China, Hong Kong, Russia (Krasnoyarsk Krai), Malaysia, Philippines, Singapore, Taiwan, most of Mongolia, Western Australia
40) UTC+09:00 Korea, East Timor, Russia (Irkutsk Oblast), Japan
41) UTC+09:30 Australia (Northern Territory);(Summer)Australia (South Australia))
42) UTC+10:00 Russia (Zabaykalsky Krai); (Summer)Australia (New South Wales, Queensland, Tasmania, Victoria)
43) UTC+10:30 Lord Howe Island
44) UTC+11:00 New Caledonia, Russia (Primorsky Krai), Solomon Islands
45) UTC+11:30 Norfolk Island
46) UTC+12:00 Fiji, Russia (Kamchatka Krai);(Summer)New Zealand
47) UTC+12:45 (Summer)New Zealand
48) UTC+13:00 Tonga
49) UTC+14:00 Kiribati (Line Islands)
*/

uint32_t timestamp = 0;


void SNTPs_init(uint8_t s, uint8_t *buf)
{
	NTPs_SOCKET = s;

	data_buf = buf;
#if 0
	uint8_t Flag;
	NTPformat.leap = 0;           /* leap indicator */
	NTPformat.version = 4;        /* version number */
	NTPformat.mode = 3;           /* mode */
	NTPformat.stratum = 0;        /* stratum */
	NTPformat.poll = 0;           /* poll interval */
	NTPformat.precision = 0;      /* precision */
	NTPformat.rootdelay = 0;      /* root delay */
	NTPformat.rootdisp = 0;       /* root dispersion */
	NTPformat.refid = 0;          /* reference ID */
	NTPformat.reftime = 0;        /* reference time */
	NTPformat.org = 0;            /* origin timestamp */
	NTPformat.rec = 0;            /* receive timestamp */
	NTPformat.xmt = 1;            /* transmit timestamp */

	Flag = (NTPformat.leap<<6)+(NTPformat.version<<3)+NTPformat.mode; //one byte Flag
	memcpy(ntpmessage,(void const*)(&Flag),1);
#else
  memset(&NTPsformat, 0, sizeof(NTPsformat));
#endif

  Nowdatetime.yy = 2020;
  Nowdatetime.mo = 5;
  Nowdatetime.dd = 19;
  Nowdatetime.hh = 0;
  Nowdatetime.mm = 0;
  Nowdatetime.ss = 0;

  timestamp = numberOfSecondsSince1900Epoch();
}

/*
* Function: convertHostToNetwork
* Paramters: ntp_timestamp
* Returns: void
* Comments: converts timestamp from host to network format
*/
void convertHostToNetwork(ntp_timestamp *ntp)
{
  ntp->second = htonl(ntp->second);
  ntp->fraction = htonl(ntp->fraction);
}


/*
* Function: convertNetworkToHost
* Paramters: ntp_timestamp
* Returns: void
* Comments: converts timestamp from network to host format
*/
void convertNetworkToHost(ntp_timestamp *ntp)
{
  ntp->second = ntohl(ntp->second);
  ntp->fraction = ntohl(ntp->fraction);
}

unsigned int getleapIndicator()
{
  return NTPsformat.leapVersionMode >> 6;
}
void leapIndicator(unsigned int newValue)
{
  NTPsformat.leapVersionMode = (0x3F & NTPsformat.leapVersionMode) | ((newValue & 0x03) << 6);
}
    
unsigned int getversionNumber()
{
  return (NTPsformat.leapVersionMode >> 3) & 0x07;
}
void versionNumber(unsigned int newValue)
{
  NTPsformat.leapVersionMode = (0xC7 & NTPsformat.leapVersionMode) | ((newValue & 0x07) << 3);
}

unsigned int getmode()
{
  return (NTPsformat.leapVersionMode & 0x07);
}
void mode(unsigned int newValue)
{
  NTPsformat.leapVersionMode = (NTPsformat.leapVersionMode & 0xF8) | (newValue & 0x07);
}
    

void reverseBytes_(uint32_t *number)
{
    char buf[4];
    char *numberAsChar = (char*)number;
    
    buf[0] = numberAsChar[3];
    buf[1] = numberAsChar[2];
    buf[2] = numberAsChar[1];
    buf[3] = numberAsChar[0];
    
    numberAsChar[0] = buf[0];
    numberAsChar[1] = buf[1];
    numberAsChar[2] = buf[2];
    numberAsChar[3] = buf[3];
}

void swapEndian()
{
  reverseBytes_(&NTPsformat.rootDelay);
  reverseBytes_(&NTPsformat.rootDispersion);
  reverseBytes_(&NTPsformat.referenceTimestampSeconds);
  reverseBytes_(&NTPsformat.referenceTimestampFraction);
  reverseBytes_(&NTPsformat.originTimestampSeconds);
  reverseBytes_(&NTPsformat.originTimestampFraction);
  reverseBytes_(&NTPsformat.receiveTimestampSeconds);
  reverseBytes_(&NTPsformat.receiveTimestampFraction);
  reverseBytes_(&NTPsformat.transmitTimestampSeconds);
  reverseBytes_(&NTPsformat.transmitTimestampFraction);

}

bool isLeapYear(uint32_t year)
{
    int multipleOfFour = (year % 4) == 0;
    int multipleOfOneHundred = (year % 100) == 0;
    int multipleOfFourHundred = (year % 400) == 0;
        
    // Formula: years divisble by 4 are leap years, EXCEPT if it's
    // divisible by 100 and not by 400.
    return (multipleOfFour && !(multipleOfOneHundred && !multipleOfFourHundred));
}

static int numDaysInMonths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

uint32_t numberOfSecondsSince1900Epoch()
{
#if 0
  Nowdatetime.yy = 2020;
  Nowdatetime.mo = 5;
  Nowdatetime.dd = 19;
  Nowdatetime.hh = 0;
  Nowdatetime.mm = 0;
  Nowdatetime.ss = 0;
#endif

  uint32_t returnValue = 0;
  returnValue = 
        Nowdatetime.ss + 
        (Nowdatetime.mm * SECONDS_IN_MINUTE) + 
        (Nowdatetime.hh * SECONDS_IN_MINUTE * MINUTES_IN_HOUR);

  uint32_t numDays = 0;
  for (uint32_t currentYear = EPOCH_YEAR; currentYear < Nowdatetime.yy; currentYear++)
  {
      if (isLeapYear(currentYear))
      {
          numDays++;
      }
  }
  numDays += DAYS_IN_YEAR * (Nowdatetime.yy - EPOCH_YEAR);
  for (uint32_t currentMonth = 0; currentMonth < Nowdatetime.mo - 1; currentMonth++)
  {
      numDays += numDaysInMonths[currentMonth];
  }
  numDays += Nowdatetime.dd - 1;
  if (isLeapYear(Nowdatetime.yy) && Nowdatetime.mo > 2)
  {
      numDays++;
  }
  returnValue += numDays * SECONDS_IN_MINUTE * MINUTES_IN_HOUR * HOURS_IN_DAY;

  return returnValue;
}

void set_timestamp(uint32_t* sec, uint32_t* frac)
{
  *sec = timestamp;
  *frac = 0;
  
  #if 0
  ntp_timestamp getCurrentTimestamp() // Gets current time and returns as an NTP timestamp
  {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    ntp_timestamp returnTimeStamp;
    memset( &returnTimeStamp, 0, sizeof( ntp_timestamp ) );
    returnTimeStamp.second = tv.tv_sec + EPOCH;
    returnTimeStamp.fraction = (uint32_t)((double)(tv.tv_usec+1) * (double)(1LL<<32) * 1.0e-6);

    return returnTimeStamp;
  }
  #endif
}

int8_t SNTPs_run()
{

//#define DEBUG_SNTPS_RUN

	uint16_t RSR_len;
	uint8_t destip[6];
	uint16_t destport;
  uint32_t recv_sec;
  uint32_t recv_frac;

	if(getSn_SR(NTPs_SOCKET) != SOCK_UDP)
        wiz_socket(NTPs_SOCKET, Sn_MR_UDP, ntp_port, 0x00);

	switch(getSn_SR(NTPs_SOCKET))
	{
  	case SOCK_UDP:
  		if ((RSR_len = getSn_RX_RSR(NTPs_SOCKET)) > 0)
  		{
  		  set_timestamp(&recv_sec, &recv_frac);
        
  			if (RSR_len > MAX_SNTP_BUF_SIZE) RSR_len = MAX_SNTP_BUF_SIZE;	// if Rx data size is lager than TX_RX_MAX_BUF_SIZE
            sock_recvfrom(NTPs_SOCKET, data_buf, RSR_len, (uint8_t *)&destip, &destport);
#if 0
        printf("NTP message : %d.%d.%d.%d(%d) %d received. \r\n", destip[0], destip[1], destip[2], destip[3], destport, RSR_len);
#endif

        memcpy(&NTPsformat, &data_buf[8], sizeof(NTPsformat));
        #ifdef DEBUG_SNTPS_RUN
        printf("NTPformat.leap %#x\r\n", NTPformat.leapVersionMode);
        printf("NTPformat.version %#x\r\n", NTPformat.leapVersionMode);
        printf("NTPformat.mode %#x\r\n", NTPformat.leapVersionMode);
        printf("NTPformat.stratum %#x\r\n", NTPformat.stratum);
        printf("NTPformat.poll %#x\r\n", NTPformat.poll);
        printf("NTPformat.precision %#x\r\n", NTPformat.precision);
        printf("NTPformat.rootdelay %#x\r\n", NTPformat.rootdelay);
        printf("NTPformat.rootdisp %#x\r\n", NTPformat.rootdisp);
        printf("NTPformat.refid %#x\r\n", NTPformat.refid);
        printf("NTPformat.reftime %#x\r\n", NTPformat.reftime);
        printf("NTPformat.org %#x\r\n", NTPformat.org);
        printf("NTPformat.rec %#x\r\n", NTPformat.rec);
        printf("NTPformat.xmt %#x\r\n", NTPformat.xmt);
        #endif

        swapEndian();
        leapIndicator(0);
        versionNumber(4);
        mode(4);
        NTPsformat.stratum = 2; // secondary reference
        #if 1
        NTPsformat.poll = 6;
        NTPsformat.precision = 0;
        NTPsformat.rootDelay= 0;
        NTPsformat.rootDispersion= 0;
        #endif
        NTPsformat.referenceId[0] = 192;
        NTPsformat.referenceId[1] = 168;
        NTPsformat.referenceId[2] = 0;
        NTPsformat.referenceId[3] = 1;

        #if 0
        //NTPsformat.referenceTimestampSeconds = 0;
        //NTPsformat.referenceTimestampFraction = 0;
        #endif

        NTPsformat.originTimestampSeconds = NTPsformat.transmitTimestampSeconds;
        NTPsformat.originTimestampFraction = NTPsformat.transmitTimestampFraction;

        NTPsformat.receiveTimestampSeconds = recv_sec;
        NTPsformat.receiveTimestampFraction = recv_frac;

        set_timestamp(&NTPsformat.transmitTimestampSeconds, &NTPsformat.transmitTimestampFraction);
 
        swapEndian();
        
        sock_sendto(NTPs_SOCKET, (uint8_t*)&NTPsformat, sizeof(NTPsformat), destip, destport);

        close_socket(NTPs_SOCKET);

  			return 1;
  		}
  		break;
  	case SOCK_CLOSED:
        wiz_socket(NTPs_SOCKET,Sn_MR_UDP,ntp_port,0);
  		break;
	}
	// Return value
	// 0 - failed / 1 - success
	return 0;
}


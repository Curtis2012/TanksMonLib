//
// tanksmon.h
//
// Contains common functions/definitions used by both sensor node and manager node.
//
// All site specific parameters should go here also so they can be modified in one place.
//
//
/*
 * 2020-03-30 Created
 *
 */

//#include <GDBStub.h>
#define vsDebug true

 //
 // Site Specific Items
 //

#define NUMTANKS 2
#define MAXPINGDISTANCE 400
#define TANKPINGDELAY 5000
#define SENDDATADELAY 30000

#define SITE "CSC/HOMEPA"

#define PSSID "COWIFI151420636/0"
#define PPWD "WiFi-89951645"
#define ALTSSID "COWIFI151420636/0"
#define ALTPWD "WiFi-89951645"

//#define PSSID "Bodega"
//#define PPWD "Sail2012"
//#define ALTSSID "COWIFI151420636/0"
//#define ALTPWD "WiFi-89951645"

#define OTAPWD "Sail2012"

#define WIFIRETRYCNT 5
#define WIFIRETRYDELAY 5000
#define WIFIREBOOT 1
#define WIFITRYALT 1

#define MQTTRETRYCOUNT 5
#define MQTTRETRYDELAY 5000
#define TOPIC "csc/homepa/tankmon_tst"
//#define TOPIC "csc/homepa/tankmon"

const char* mqtt_server = "soldier.cloudmqtt.com";
const int   mqtt_port = 12045;
const char* mqtt_uid = "afdizojo";
const char* mqtt_pwd = "hfi3smlfeSqV";
const char* ssid = PSSID;
const char* password = PPWD;

char auth[] = "WlBesaF6zbb7UILuGZai66-sxEc4LM4l";  // Blynk auth token for test node
//char auth[] = "7Ug7RmlsSCihlGMKlVAGxXrB8qbNoGn9";  // Blynk auto token for casa collins node

class tank {
public:
	float depth = 0;
	float vCM = 0;        // volume in liters per cm of height
	float liquidDepth = 0;
	float liquidDepthSum = 0;
	float liquidDepthAvg = 0.0;
	float liquidVolume = 0;
	float liquidVolumeAvg = 0;
	float sensorOffset = 0;
	float loAlarm = 0.10F;  // default to 10%
	float hiAlarm = 1.10F;  // default to 110%
	int pingCount = 0;
	std::uint8_t alarmFlags = 0b00000000;
	std::uint8_t alarmFlags_prev = 0b00000000;

	tank()
	{

	}

	tank(float depth, float vCM)
	{
		this->depth = depth;
		this->vCM = vCM;
	}

	tank(float depth, float vCM, float sO)
	{
		this->depth = depth;
		this->vCM = vCM;
		this->sensorOffset = sO;
	}

	tank(float depth, float vCM, float loAlarm, float hiAlarm)
	{
		this->depth = depth;
		this->vCM = vCM;
		this->loAlarm = loAlarm;
		this->hiAlarm = hiAlarm;
	}
};

tank tanks[NUMTANKS] = {
  tank(95.25, 10.8, 21.0), // tank depth cm, volume/cm, sensor offset cm
  tank(95.5, 10.8, 19.0)
};


bool useAvg = false;
bool imperial = false;
bool writeLog = false;
bool debug = false;

// ==== End Site Specific Items ====


//
// Common items
//


#define MAXPAYLOADSIZE 256
#define MAXJSONSIZE 256
#define MSGBUFFSIZE 512

#define CVTFACTORGALLONS 0.26417F
#define CVTFACTORINCHES  0.39370F

// Alarm related bit masks & structs

#define CLEARALARMS   0b00000000
#define HIALARM       0b00000001
#define LOALARM       0b00000010
#define MAXDEPTH      0b00000100
#define NUMALARMS 4



#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <TimeLib.h>
#include "timer.h"                          // by Michael Contreras

char payload[MAXPAYLOADSIZE] = "";
char nodename[20] = "";
char msgbuff[MSGBUFFSIZE] = "";
int msgn = 0;


//
// NTP Servers:
//

static const char ntpServerName[] = "us.pool.ntp.org";
//static const char ntpServerName[] = "time.nist.gov";
//static const char ntpServerName[] = "time-a.timefreq.bldrdoc.gov";
//static const char ntpServerName[] = "time-b.timefreq.bldrdoc.gov";
//static const char ntpServerName[] = "time-c.timefreq.bldrdoc.gov";

//
// Time Zones
//

//const int timeZone = 1;     // Central European Time
const int timeZone = -5;  // Eastern Standard Time (USA)
//const int timeZone = -4;  // Eastern Daylight Time (USA)
//const int timeZone = -8;  // Pacific Standard Time (USA)
//const int timeZone = -7;  // Pacific Daylight Time (USA)


WiFiUDP Udp;
unsigned int localPort = 8888;  // local port to listen for UDP packets


struct alarm {
	std::uint8_t alarmType;
	char alarmName[10];
};

alarm alarms[NUMALARMS] = {
  {HIALARM, "HI"},
  {LOALARM, "LO"},
  {MAXDEPTH, "MAXDEPTH"},
  {CLEARALARMS, "CLEARALL"}
};

bool globalAlarmFlag = false;

WiFiClient net;
PubSubClient client(net);

// JSON Message Definition

StaticJsonDocument<MAXJSONSIZE> tankmsg;

/*  JSON Message Structure
  {

	"n"         // node name
	"t"         // tank number
	"d"         // depth
	"vCM"       // volume per centimeter
	"lD"        // liquid depth
	"lDAvg      // liquid depth average
	"lV"        // liquid volume
	"lvAvg      // liquid volume average
	"sO"        // sensor offset
	"loA"       // lo alarm level
	"hiA"       // hi alarm level
	"aF"        // alarm flags
  }
*/


//
// Forward Declaratioins
//

time_t getNtpTime();
void sendNTPpacket(IPAddress& address);

//
// Functions
//

void setupOTA()
{
	ArduinoOTA.setHostname(nodename);
	ArduinoOTA.setPassword(OTAPWD);
	ArduinoOTA.begin(WiFi.localIP());
	if (debug && !vsDebug)
	{
		Serial.print("\nOTA Ready, host name = ");
		Serial.println(ArduinoOTA.getHostname());
	}
}


void setupNTP()
{
	if (debug && !vsDebug)
	{
		Serial.println("\nNTP Setup...\n\nwaiting for NTP server sync");
	}
	setSyncProvider(getNtpTime);
	setSyncInterval(300);
}

void connectWiFi()
{
	int i = 0;

	if (!vsDebug) Serial.println("\nWifi Setup...");
	WiFi.persistent(false);
	WiFi.mode(WIFI_STA);
	WiFi.begin(ssid, password);
	wifi_station_set_auto_connect(true);
	if (!vsDebug) Serial.print("\nWiFi connecting to ");
	if (!vsDebug) Serial.print(ssid);
	while (WiFi.status() != WL_CONNECTED) {
		if (!vsDebug) Serial.print(".");
		delay(WIFIRETRYDELAY);
		WiFi.begin(ssid, password);
		i++;
		if (i > WIFIRETRYCNT)
		{
			i = 0;
			if (WIFITRYALT && (ssid != ALTSSID))
			{
				ssid = ALTSSID;
				password = ALTPWD;
				if (!vsDebug)
				{
					Serial.print("\nWiFi connecting to alt SSID: ");
					Serial.print(ssid);
				}
			}
			else if (WIFIREBOOT)
			{
				if (!vsDebug) Serial.print("\nRebooting...");
				ESP.restart();
			}
		}
	}
	if (!vsDebug)
	{
		Serial.print("\nWiFi connected to ");
		Serial.print(ssid);

		Serial.print("\nIP ");
		Serial.println(WiFi.localIP());
		Serial.println("Starting UDP");
	}
	Udp.begin(localPort);
	if (!vsDebug)
	{
	Serial.print("Local port: ");
	Serial.println(Udp.localPort());
	}
}

void connectMQTT(bool subscribe)
{
	int retry_cnt = 0;

	if (!vsDebug)
	{
		Serial.print("\nConnecting to MQTT Server: ");
		Serial.println(mqtt_server);
	}
	while (!client.connected()) {

		// Attempt to connect

		if (client.connect(nodename, mqtt_uid, mqtt_pwd))
		{
			if (!vsDebug) Serial.println("MQTT connected");
			if (subscribe)
			{
				client.subscribe(TOPIC);
				if (!vsDebug)
				{
					Serial.print("MQTT topic subscribed: ");
					Serial.print(TOPIC);
				}
			}

			retry_cnt = 0;
		}
		else
		{
			if (!vsDebug)
			{
				Serial.print("failed, rc=");
				Serial.print(client.state());
				Serial.println(" try again in 5 seconds");
			}
			delay(MQTTRETRYDELAY);
			retry_cnt++;
		}

		if (retry_cnt > MQTTRETRYCOUNT)
		{
			if (!vsDebug)
			{
				Serial.println();
				Serial.println("Rebooting...");
			}
			ESP.restart();
		}
	}
}

void setupMQTT(bool mqttsubscribe, std::function<void(char*, uint8_t*, unsigned int)> callback)
{
	if (!vsDebug)
	{
		Serial.println();
		Serial.println("MQTT Setup...");
	}
	client.setServer(mqtt_server, 12045);
	client.setCallback(callback);
	connectMQTT(mqttsubscribe);
}

int mapAlarm(std::uint8_t alarmType)
{
	switch (alarmType) {
	case HIALARM:
		return (0);

	case LOALARM:
		return (1);

	case MAXDEPTH:
		return (2);

	case CLEARALARMS:
		return (3);

	default: return (-1);

	}
}

String timestampString()
{
	String timestamp = "";
	char time24[8] = "";
	int n = 0;

	n = sprintf(time24, "%02d:%02d:%02d", hour(), minute(), second());

	//timestamp = String(year()) + "-" + String(month()) + "-" + day() + " " + hour() + ":" + minute() + ":" + second();
	timestamp = String(year()) + "-" + String(month()) + "-" + day() + " " + String(time24);

	return(timestamp);

}


/*-------- NTP code ----------*/

const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
#define NTPRETRYCOUNT 3
	IPAddress ntpServerIP; // NTP server's ip address
	int i = 0;


	while (Udp.parsePacket() > 0); // discard any previously received packets

	while (i++ < NTPRETRYCOUNT)
	{
		if (!vsDebug) Serial.println("Sending NTP Request");
		// get a random server from the pool
		WiFi.hostByName(ntpServerName, ntpServerIP);
		if (!vsDebug)
		{
			Serial.print(ntpServerName);
			Serial.print(": ");
			Serial.println(ntpServerIP);
		}
		sendNTPpacket(ntpServerIP);
		uint32_t beginWait = millis();
		while (millis() - beginWait < 3000)
		{
			int size = Udp.parsePacket();
			if (size >= NTP_PACKET_SIZE)
			{
				if (!vsDebug) Serial.println("Received NTP Response");
				Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
				unsigned long secsSince1900;
				// convert four bytes starting at location 40 to a long integer
				secsSince1900 = (unsigned long)packetBuffer[40] << 24;
				secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
				secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
				secsSince1900 |= (unsigned long)packetBuffer[43];
				return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
			}
		}
		if (!vsDebug) Serial.println("No NTP Response, retrying");
	}

	if (!vsDebug) Serial.println("No NTP Response.");
	return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress& address)
{
	// set all bytes in the buffer to 0
	memset(packetBuffer, 0, NTP_PACKET_SIZE);
	// Initialize values needed to form NTP request
	// (see URL above for details on the packets)
	packetBuffer[0] = 0b11100011;   // LI, Version, Mode
	packetBuffer[1] = 0;     // Stratum, or type of clock
	packetBuffer[2] = 6;     // Polling Interval
	packetBuffer[3] = 0xEC;  // Peer Clock Precision
	// 8 bytes of zero for Root Delay & Root Dispersion
	packetBuffer[12] = 49;
	packetBuffer[13] = 0x4E;
	packetBuffer[14] = 49;
	packetBuffer[15] = 52;
	// all NTP fields have been given values, now
	// you can send a packet requesting a timestamp:
	Udp.beginPacket(address, 123); //NTP requests are to port 123
	Udp.write(packetBuffer, NTP_PACKET_SIZE);
	Udp.endPacket();
}
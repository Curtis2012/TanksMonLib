//
// tanksmon.h
//
// Contains common functions/definitions used by both sensor node and manager node.
//
// All site specific parameters should go here also so they can be modified in one place.
//
//
/*
 * 2020-03-30 C. Collins. Created
 * 2020-04-20 C. Collins. SPIFF/JSON config file support added
 * 2020-10-10 C. Collins. Added PumpMon integration/low water shutoff logic
 * 2020-10-23 C. Collins. Converted to dynamic memory allocation for tanks/sensors
 * 2020-12-08 C. Collins. (in work) Added tank level persistence. This was needed to handle the situation where the long deep sleeping propane tank monitor and manager node get out of sync. 
 *                        For example, propane monitor is in deep sleep when manager node is rebooted. In this case the manager node will restart with propane tank level of 0 and may not receive
 *                        an update for a long time. This results in tank level of 0 being displayed until an update is received (which could be hours in the case of propane monitor). The persist file
 *                        allows the manager node to load the last known good level reading. 
 *
 */

#include "FS.h"
#include <ArduinoJson.h>
#include <NewPingESP8266.h>
#include <TimeLib.h>
#include "timer.h"          // by Michael Contreras

#define TANKSMONCFGFILE  "/tanksmoncfg.json"
#define TANKSMONPERSISTFILE  "/tanksmonpersist.json"
#define JSONCONFIGDOCSIZE  4000
#define JSONPERSISTDOCSIZE  50

byte configBuff[JSONCONFIGDOCSIZE];
File configFile;
StaticJsonDocument<JSONCONFIGDOCSIZE> configDoc;

byte presistBuff[JSONPERSISTDOCSIZE];
File persistFile;
StaticJsonDocument<JSONPERSISTDOCSIZE> persistDoc;

/*
Persist Doc Structure

{
{
"tanklevels":
	[
	{
	"level":0
	},
	{
	"level":0
	},

	{
	"level":0
	},

	{
	"level":0
	}
	...
	]
}

*/




//
// Site Specific Items =========================================================================================================
//

#define MAXPINGDISTANCE 400
#define SENDDATADELAY 30000

int numtanks = 0;
int startingTankNum = -1;

long tankpingdelay = 5000L;
const char* sitename = " ";

bool dst = false;
bool useAvg = false;
bool imperial = false;

const char* blynkAuth = "GBCY5avvW45A24xyW7J_TXNegXxv89q-";  // Blynk auth token for test node 2

//const char* blynkAuth = "WlBesaF6zbb7UILuGZai66-sxEc4LM4l";  // Blynk auth token for test node
//char auth[] = "7Ug7RmlsSCihlGMKlVAGxXrB8qbNoGn9";  // Blynk auto token for casa collins node


class tank {
public:
	const char* tankType = "W";
	bool ignore = false;
	float depth = 0;
	float vCM = 0;        // volume in liters per cm of height
	float liquidDepth = 0;
	float liquidDepthSum = 0;
	float liquidDepthAvg = 0.0;
	float liquidVolume = 0;
	float liquidVolumeAvg = 0;
	float percentFull = 0;  // for propane tank type
	int sonarOffset = 0;
	uint32_t sonarTrigPin = 0;
	uint32_t sonarEchoPin = 0;
	NewPingESP8266* sonar;
	float loAlarm = 0.10F;  // default to 10%
	float hiAlarm = 1.10F;  // default to 110%
	int pingCount = 0;
	unsigned long lastMsgTime = 0L;
	unsigned long timeOut = 60000L;  // default time out value
	std::uint8_t alarmFlags = 0b00000000;
	std::uint8_t alarmFlags_prev = 0b00000000;
	long int pumpNode = 0;
	int pumpNumber = 0;

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
		this->sonarOffset = sO;
	}

	tank(float depth, float vCM, float sO, float loAlarm, float hiAlarm)
	{
		this->depth = depth;
		this->vCM = vCM;
		this->sonarOffset = sO;
		this->loAlarm = loAlarm;
		this->hiAlarm = hiAlarm;
	}
};

//tank tanks[NUMTANKS];

tank* tanks;


// ==== End Site Specific Items ====


//
// Common items
//


#define MAXPAYLOADSIZE 4000
#define MAXJSONSIZE 4000
#define MSGBUFFSIZE 80

#define CVTFACTORGALLONS 0.26417F
#define CVTFACTORINCHES  0.39370F

// Alarm related bit masks & structs

#define CLEARALARMS   0b00000000
#define HIALARM       0b00000001
#define LOALARM       0b00000010
#define MAXDEPTH      0b00000100
#define NUMALARMS 4

float loAlarmFactor = 0.10;
float hiAlarmFactor = 1.10;

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

// JSON Message Definition

StaticJsonDocument<MAXJSONSIZE> tankmsg;

/*  JSON Message Structure
  {
	"n"         // node name
	"t"         // tank number
	"tT"        // tank type
	"d"         // depth
	"vCM"       // volume per centimeter
	"lD"        // liquid depth
	"lDAvg"      // liquid depth average
	"lV"        // liquid volume
	"lvAvg"      // liquid volume average
	"pF"        // percent full (for propane tank type)
	"sO"        // sonar offset
	"loA"       // lo alarm level
	"hiA"       // hi alarm level
	"aF"        // alarm flags
  }
*/


//
// Functions
//

void dumpTanksStruct()
{
	outputMsg("\nTanks struct dump:");
	for (int t = 0; t <  numtanks; t++)
	{
		msgn = snprintf(msgbuff, MSGBUFFLEN, "\ntanks[%i].tankType=%c\ntanks[%i].ignore=%i\ntanks[%i].timeOut=%i", t, tanks[t].tankType[0], t, tanks[t].ignore, t, tanks[t].timeOut);
		Serial.print(msgbuff);
		msgn = snprintf(msgbuff, MSGBUFFLEN, "\ntanks[%i].sonarOffset=%i\ntanks[%i].sonarTrigPin=%i\ntanks[%i].sonarEchoPin=%i", t, tanks[t].sonarOffset, t, tanks[t].sonarTrigPin, tanks[t].sonarTrigPin, t, tanks[t].sonarEchoPin);
		Serial.print(msgbuff);
		msgn = snprintf(msgbuff, MSGBUFFLEN, "\ntanks[%i].depth=%f\ntanks[%i].vCM=%f", t, tanks[t].depth, t, tanks[t].vCM);
		Serial.print(msgbuff);
		msgn = snprintf(msgbuff, MSGBUFFLEN, "\ntanks[%i].loAlarm=%f\ntanks[%i].hiAlarm=%f\ntanks[%i].pumpNode=%i", t, tanks[t].loAlarm, t, tanks[t].hiAlarm, t, tanks[t].pumpNode);
		Serial.print(msgbuff);
		msgn = snprintf(msgbuff, MSGBUFFLEN, "\ntanks[%i].pumpNumber=%i\n", t, tanks[t].pumpNumber);
		Serial.print(msgbuff);
		Serial.flush();
	}
}

bool openPersistFile()
{
	Serial.print("\nOpening persist file: ");
	Serial.print(TANKSMONPERSISTFILE);
	configFile = SPIFFS.open(TANKSMONPERSISTFILE, "r+");
	if (!persistFile)
	{
		Serial.println("Failed to open persist file");
		if (!SPIFFS.exists(TANKSMONCFGFILE)) Serial.println("File does not exist");
		return(false);
	}
	else return(true);

	return(false);
}


bool loadConfig()
{
	DeserializationError jsonError;
	size_t size = 0;
	int t = 0;

	Serial.println("Mounting FS...");
	if (!SPIFFS.begin())
	{
		Serial.println("Failed to mount file system");
		return false;
	}
	else Serial.println("Mounted file system");

	Serial.print("\nOpening config file: ");
	Serial.print(TANKSMONCFGFILE);
	configFile = SPIFFS.open(TANKSMONCFGFILE, "r");
	if (!configFile)
	{
		Serial.println("Failed to open config file");
		if (!SPIFFS.exists(TANKSMONCFGFILE)) Serial.println("File does not exist");
		return false;
	}

	size = configFile.size();
	Serial.print("File size = ");
	Serial.println(size);

	configFile.read(configBuff, size);

	jsonError = deserializeJson(configDoc, configBuff);
	if (jsonError)
	{
		Serial.println("Failed to parse config file");
		switch (jsonError.code()) {
		case DeserializationError::Ok:
			Serial.print(F("Deserialization succeeded"));
			break;
		case DeserializationError::InvalidInput:
			Serial.print(F("Invalid input!"));
			break;
		case DeserializationError::NoMemory:
			Serial.print(F("Not enough memory"));
			break;
		default:
			Serial.print(F("Deserialization failed"));
			break;
		}
		return false;
	}
	Serial.println("\nPretty dump of config file: \n");
	size_t uartsz = 128;
	serializeJsonPretty(configDoc, msgbuff, MSGBUFFSIZE);
	outputMsg(msgbuff);

	sitename = configDoc["site"]["sitename"];
	pssid = configDoc["site"]["pssid"];
	timeZone = configDoc["site"]["timezone"];
	dst = configDoc["site"]["dst"];
	ppwd = configDoc["site"]["ppwd"];
	wifiTryAlt = configDoc["site"]["usealtssid"];
	assid = configDoc["site"]["altssid"];
	apwd = configDoc["site"]["altpwd"];
	mqttTopicData = configDoc["site"]["mqtt_topic_data"];
	mqttTopicCtrl = configDoc["site"]["mqtt_topic_ctrl"];
	mqttUid = configDoc["site"]["mqtt_uid"];
	mqttPwd = configDoc["site"]["mqtt_pwd"];
	otaPwd = configDoc["site"]["otapwd"];

	numtanks = configDoc["site"]["numtanks"];
	tanks = new tank[numtanks];

	startingTankNum = configDoc["site"]["startingTankNum"];

	//displayUpdateDelay = configDoc["site"]["displaydelay"];
	imperial = configDoc["site"]["imperial"];
	useAvg = configDoc["site"]["useavg"];
	debug = configDoc["site"]["debug"];
	tankpingdelay = configDoc["site"]["tankpingdelay"];
	// senddatadelay = configDoc["site"]["senddatadelay"];
	otaPwd = configDoc["site"]["otapwd"];
	blynkAuth = configDoc["site"]["blynkauthtoken"];

	for (int t = 0; t < numtanks; t++)
	{
		tanks[t].tankType = configDoc["tankdefs"][t]["tankType"];
		tanks[t].ignore = configDoc["tankdefs"][t]["ignore"];
		tanks[t].timeOut = configDoc["tankdefs"][t]["timeout"];
		tanks[t].timeOut *= 1000;   // timeout is stored in config file in seconds, convert to milliseconds
		tanks[t].depth = configDoc["tankdefs"][t]["depth"];
		tanks[t].vCM = configDoc["tankdefs"][t]["vCM"];

		tanks[t].sonarOffset = configDoc["tankdefs"][t]["sensorOffset"];

		tanks[t].sonarTrigPin = configDoc["tankdefs"][t]["sonarTrigPin"];
		tanks[t].sonarEchoPin = configDoc["tankdefs"][t]["sonarEchoPin"];

		loAlarmFactor = configDoc["tankdefs"][t]["loAlarmFactor"];
		tanks[t].loAlarm = loAlarmFactor * tanks[t].depth;
		hiAlarmFactor = configDoc["tankdefs"][t]["hiAlarmFactor"];
		tanks[t].hiAlarm = hiAlarmFactor * tanks[t].depth;
		tanks[t].pumpNode = configDoc["tankdefs"][t]["pumpnode"];
		tanks[t].pumpNumber = configDoc["tankdefs"][t]["pumpnumber"];

	}

	Serial.println("Config loaded");
	if (debug) dumpTanksStruct();

	return true;
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



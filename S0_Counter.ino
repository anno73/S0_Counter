// USE_GITHUB_USERNAME=anno73
/*-------------------------------------------------------
    xAP S0 Electric Energy Counter 

Count S0 Pulses
Send to xAP network

Used Pins:
		Uno		Leonardo
    Serial:     FTDI Prog + Debug
        D0      -		RX
        D1      -		TX

    SPI:        Ethernet/SDCard
        D13     -		SCK
        D12     -		MISO
        D11     -		MOSI
        
        D10     SS Ethernet
        D4      SS SD Card

    Display:    IIC/TWI
        A4      D2		SDA
        A5      D3		SCL
        D3      		INT1     IIC Interrupt
        D5      		PWM      LCD Backlight

    ModBus:     SoftSerial/Serial1
        D2      D0		INT0	RX
        D6      D1				TX
        D7      D7		Direction

    S0 IRQ Inputs:
        D8      S0 4
        D9      S0 5
        A0      S0 0
        A1      S0 1
        A2      S0 2
        A3      S0 3
		*D4		S0 6	Potentially
		*D5		S0 7	Potentially
                .

Send regular updates to xPL:
    - Every minute or
    - *Every ?kWh

ToDo:
    - Send on every kWh
    - Read config from EEPROM
		- MAC
		- IP
		- xPL instance name
		- xPL port
		- xPL broadcast IP
		- xPL heartbeat interval
    - Change config via xPL message
	- Preset counters via xPL message
	- Brownout detection - persist counters + config to EEProm

/*-------------------------------------------------------*/

#define PIN_S0_0 A0     // FBH
#define PIN_S0_1 A1     // Alarm
#define PIN_S0_2 A2     // KWL
#define PIN_S0_3 A3     // KNX
#define PIN_S0_4 8      // Daten
#define PIN_S0_5 9      // Main

// http://www.pjrc.com/teensy/td_libs_Time.html
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
// http://code.google.com/p/livebox-hah/source/browse/#svn%2Ftrunk%2Fuserapps%2Farduino%2Flibraries%2FxAP
// #include <xAP.h>
// http://code.google.com/p/oopinchangeint/
#include <ooPinChangeInt.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>
#include "S0Impulse.h"
#include <string.h>
#include <avr/io.h>
#include <avr/pgmspace.h>

// Streaming c++ like output
// http://playground.arduino.cc/Main/StreamingOutput
// Serial << "text 1" << someVariable << "Text 2" ... ;
template<class T> inline Print &operator <<(Print &obj, T arg) { obj.print(arg); return obj; }

#define FIRMWARE_VERSION "0.0.2.0"

#define EEPROM_MAIN_BASE			0
#define EEPROM_MAIN_FLAGS			0
#define EEPROM_MAIN_IP				2
#define EEPROM_MAIN_				6
#define EEPROM_MAIN_XPL_INSTANCE	16

// Flags
#define EEPROM_MAIN_USE_DHCP	0

/*----------------------------------------------------------------------
    Ethernet related structures
/*----------------------------------------------------------------------*/
byte mac[] = {
    0x02, 0x00, 0x00, 0x00, 0x00, 0x01
};

byte ip[] = { 192, 168, 2, 21 };

EthernetUDP Udp;

//uint8_t packetBuffer[512];

/*----------------------------------------------------------------------
    xPL related structures
/*----------------------------------------------------------------------*/

#define XPL_HEARTBEAT_INTERVAL    10000L

#define XPL_VENDOR		"ah"			// max 8 chars
#define XPL_DEVICE		"energy"		// max 8 chars
#define XPL_INSTANCE	"counter1"		// max 16 chars

#define XPL_SOURCE      XPL_VENDOR "-" XPL_DEVICE
#define XPL_ENDPOINT    XPL_SOURCE "." XPL_INSTANCE

unsigned int xPLPort = 3865;        // xPL Port to listen on and send to
IPAddress xPLBroadcast(255, 255, 255, 255);

uint8_t seenOwnHeartbeat = 0;

/*----------------------------------------------------------------------
    S0 related structures
/*----------------------------------------------------------------------*/
#define MAX_S0_INPUTS 8

S0Impulse S0[MAX_S0_INPUTS];

//#define S0BusUpdateIntervall    (60 * 1000)
#define S0BusUpdateIntervall    (5 * 1000)


/*----------------------------------------------------------------------
    Function declarations
/*----------------------------------------------------------------------*/
void setup(void);
void loop(void);
void initEEPROM(uint8_t value, uint16_t from, uint16_t count);
void dumpEEPROM(uint16_t from, uint16_t count);

/*----------------------------------------------------------------------
    Arduino Setup
/*----------------------------------------------------------------------*/
void setup() {

	wdt_disable();

    Serial.begin(115200);
    
    // Dump internal config on startup
    Serial << F(
        "Welcome to S0 Electric Energy Counter\n\n"
        "My name is: " XPL_ENDPOINT "\n"
        "xPL Port: ") << xPLPort << F("\n"
    );
   
//    while (! Serial);    // For Leonardo

    // Start Ethernet connection
#if 1
    Serial << F("Init Ethernet - DHCP...\n");
    if (Ethernet.begin(mac) == 0)		// DHCP
    {
        for (;;)
		{
			Serial << F("Failed to get IP via DHCP. Shutting down.\n");
			delay(1000);
		}
    }
    Serial << F("DHCP gave IP Address: ") << Ethernet.localIP() << "\n";
#else
	Ethernet.begin(mac, ip);		// Fixed
    Serial << F("Using IP Address: ") << Ethernet.localIP() << "\n";
#endif



//	Serial << F("Free RAM: ") << freeRam() << '\n';

    Udp.begin(xPLPort);
    
    // Initialize S0 counters
    Serial << F("Init S0 counters...\n");
    S0[0] = S0Impulse(PIN_S0_0, 0);		// FBH
    S0[1] = S0Impulse(PIN_S0_1, 1);		// Alarm
    S0[2] = S0Impulse(PIN_S0_2, 2);		// KWL
    S0[3] = S0Impulse(PIN_S0_3, 3);		// KNX
    S0[4] = S0Impulse(PIN_S0_4, 4);		// Daten
    S0[5] = S0Impulse(PIN_S0_5, 5);		// Main
    S0[6] = S0Impulse(PIN_S0_4, 6);
    S0[7] = S0Impulse(PIN_S0_5, 7);

//	initEEPROM();
#if 0

    S0[0].setCountLowPerHigh(2000);		// FBH
    S0[0].setName("S0_FBH");
    S0[1].setCountLowPerHigh(2000);		// Alarm
    S0[1].setName("S1_Alarm");
    S0[2].setCountLowPerHigh(2000);		// KWL
    S0[2].setName("S2_KWL");
    S0[3].setCountLowPerHigh(2000);		// KNX
    S0[3].setName("S3_KNX");
    S0[4].setCountLowPerHigh(2000);		// Daten
    S0[4].setName("S4_EDV");
    S0[5].setCountLowPerHigh(400);		// Main
    S0[5].setName("S5_Main");
    S0[6].setCountLowPerHigh(2000);
    S0[6].setName("S6_Whatever");
    S0[7].setCountLowPerHigh(2000);
    S0[7].setName("S7_Whatever");

    // Set all to active
	// ### callback/byte ptr for update request
    for (uint8_t i = 0; i < MAX_S0_INPUTS; i++)
    {
//		S0[i].setActive();
    }

	
    // Set all to active
	// ### callback/byte ptr for update request
    for (uint8_t i = 0; i < MAX_S0_INPUTS; i++)
    {
		S0[i].setActive();
    }

	dumpEEPROM(0, 512);

#endif



    Serial << F("Init watchdog...\n");
    wdt_enable(WDTO_2S);

	// xPL standard advises to:
	// http://xplproject.org.uk/wiki/index.php?title=XPL_Specification_Document#Heartbeat_behaviour_at_initial_device_startup
	// Send xPL heartbeats every 3 .. 10 seconds and wait for our own echo
	// If there is no ersponse within some minutes switch to 30s interval
	// This should go here
	
	// while (! seenOwnHeartbeat); ... blah blah
	
    Serial <<F("Init done. Let's rock...\n");

}   // setup


/*----------------------------------------------------------------------
    Arduino Main Loop
/*----------------------------------------------------------------------*/

//time_t oldTime;

uint32_t nextUpdateOnBusTS = millis() + S0BusUpdateIntervall;

uint32_t nextDTTMRequest = millis() + 5000;

uint32_t nextS0Increment = millis() + 1000;

uint32_t nextXplHeartbeat = millis() + XPL_HEARTBEAT_INTERVAL;

uint32_t msNow;

void loop() 
{

    wdt_reset();
    
    msNow = millis();

//	Serial << F("Free RAM: ") << freeRam() << '\n';
    
	// Check if we possibly got a new UDP packet to process
    if (Udp.parsePacket() > 0)
    {
        processXplMessage();
    }

	// Check for heartbeat interval due
    if (msNow >= nextXplHeartbeat)
    {
        nextXplHeartbeat = msNow + XPL_HEARTBEAT_INTERVAL;

        sendXplHeartbeat();       
    }

	// Generate some test data internally
    if (msNow >= nextS0Increment)
    {
        nextS0Increment = msNow + 1000;

        for (uint8_t i = 0; i < MAX_S0_INPUTS; i++)
        {
            S0[i].cbmethod();
        }
    }

    // Every S0BusUpdateIntervall millis send update of values on bus
    if (msNow >= nextUpdateOnBusTS)
    {
        nextUpdateOnBusTS = msNow + S0BusUpdateIntervall;

        for (uint8_t i = 50; i; i--) Serial << '-';
//        Serial << '\n' << year() << month() << day() << hour() << minute() << second() << ':' << now();
        Serial << '\n' << F("S0 update to bus:\n");

        for (uint8_t i = 0; i < MAX_S0_INPUTS; i++)
        {
            if (S0[i].getIsActive())
            {
				Serial << F("countLow: ") << S0[i].getCountLow() << '\n';
                sendXplS0TriggerMsg(&S0[i]);
            }
            else Serial << "Not active: S0[" << i << "] = " << S0[i].getID() << '\n'; 
        }

//        for (uint8_t i = 0; i < MAX_S0_INPUTS; i++) Serial << S0[i].getID() << ":" << S0[i].getCountLowPerHigh() << ":" << S0[i].getCountHigh() << "." << S0[i].getCountLow() << ":" << S0[i].getCount() << "\n";
//        for (uint8_t i = 0; i < MAX_S0_INPUTS; i++) Serial << S0[i].getID() << ":" << S0[i].getCount() << "\n";
    }
}   // loop

#if 1		// xPL related code
//--------------------------------------------------------
// xPL Related Code
//--------------------------------------------------------

uint8_t XplReadLine(char * buff, uint8_t buffLen)
{
	char * cp = buff;
	uint8_t size = 0;
	
	buff[buffLen] = 0;
	
	for (; buffLen; buffLen--)
	{
	
		*cp = Udp.read();
		
//		Serial << F("XplReadLine: ") << size << ':' << buff << ".\n";
		
		if (*cp == '\n')
		{
//			*cp = 0;
//			return size;
			break;
		}
	
		cp++;
		size++;
	}

	*cp = 0;
	
	Serial << F("XplReadLine: ") << size << ':' << buff << ".\n";

	return size;
}	// XplReadLine

/*---------------------------------------------------------------------
	processXplMessage

	http://xplproject.org.uk/wiki/index.php?title=XPL_Specification_Document
	
	Try to parse an xPL message and possibly trigger some actions.
	
---------------------------------------------------------------------*/
void processXplMessage(void)
{
	#define XPL_READLN_BUFFER_SIZE 45
	char buffer[XPL_READLN_BUFFER_SIZE];

    uint16_t packetSize = Udp.available();
    if (! packetSize)
        return;
    
    for (uint8_t i = 50; i; i--) Serial << '-';
    Serial << F("\nReceived packet of size ") << packetSize;
    Serial << F("\nFrom ") << Udp.remoteIP() << ':' << Udp.remotePort() << '\n';

	uint8_t msgType = 0;
	uint8_t broadcast = 0;
	uint8_t unicast = 0;
	char * msgType_cp;
	char * hop_cp;
	char * source_cp;
	char * target_cp;
	char * class_cp;
	char * command_cp;
	
//	char * cp = (char *) buffer;
	uint8_t readChar;
	
	
	/* 
		Message type identifier
		8 Bytes, terminated with '\n'
		Valid values are: 
			xpl-cmnd
			xpl-stat
			xpl-trig	- ignore
	*/
	// Get message type
	readChar = XplReadLine(buffer, 32);
	Serial << readChar << ':' << buffer << ".\n";
//	Serial << F("UDP Available: ") << Udp.available() << ".\n";
	// Needs to be exactly 8 bytes
	if (readChar != 8)
	{
		Serial << F("Not xPL: Message Type Length: ") << readChar << ".\n";
		return;
	}
	
	// Must start off with 'xpl-'
	if (strncmp_P(buffer, PSTR("xpl-cmnd"), 8) == 0)
	{
		msgType = 'c';
	} 
	else if (strncmp_P(buffer, PSTR("xpl-stat"), 8) == 0)
	{
		msgType = 's';

	}
	else if (strncmp_P(buffer, PSTR("xpl-trig"), 8) == 0)
	{
		msgType = 't';
	} 
	else 
	{
		Serial << F("Not xPL: Message Type: ") << buffer << ".\n";
		return;
	}

	Serial << F("xPL Message Type: ") << buffer << " : " << (char) msgType << ".\n";

	// '{\n'
	XplReadLine(buffer, 4);
	
	// 'hop=1'
	XplReadLine(buffer, 8);

	// 'source=###-###.###'
	// 7 + 8 + '-' + 8 + '.' + 16 + '\n'
	readChar = XplReadLine(buffer, 45);

	// 'target=###-###.###'
	// 7 + 8 + '-' + 8 + '.' + 16 + '\n'
	// target may be '*' for broadcast
	// of my name
	readChar = XplReadLine(buffer, 45);
	
	broadcast = unicast = 0;
	if (readChar == 7 && buffer[7] == '*')
	{
		broadcast = 1;
	}
	
	if (! broadcast)
	{
		// fetch own name from eeprom and compare
//		if (strncmp(buffer[7], XPL_ENDPOINT, sizeof(XPL_ENDPOINT)) == 0)		// ### hardcoded instance id ... !!!
		if (strncmp((const char *) &buffer[7], "mhouse.item", sizeof("mhouse.item")) == 0)		// ### hardcoded instance id ... !!!
		{
			unicast = 1;
		}
	}
	
	if (! broadcast && ! unicast)
	{
		Serial << F("xPL Message not for me: Target: ") << &buffer[7] << " : " << readChar << ".\n";
		return;
	}

	Serial << F("xPL Message Target: ") << buffer << " : " << readChar << ".\n";

	// '}'
	XplReadLine(buffer, 4);

	/*
		xPL Message Class
		###.###
		8 + '.' + 8 + '\n'

		hbeat.basic
		hbeat.app
		hbeat.request		<-
		sensor.request
		config.list
		config.current
		config.response

	*/
	readChar = XplReadLine(buffer, 20);
	if (readChar > 17)
		return;
	
	if (strncmp_P(class_cp, PSTR("hbeat.request"), sizeof("hbeat.request")) == 0)
	{
		Serial << F("Got a 'hbeat.request' message.\n");
		
		// '{\n'
		XplReadLine(buffer, 4);
		
		readChar = XplReadLine(buffer, XPL_READLN_BUFFER_SIZE);
		if (strncmp_P(buffer, PSTR("command=request"), sizeof("command=request")) != 0)
		{
			return;
		}

		Serial << F("Go to send a heartbeat message.\n");
			
		sendXplHeartbeat();
		return;		
	}
	else if (strncmp(class_cp, "config.list", sizeof("config.list")) == 0)
	{
		Serial << F("Got a 'config.list' message\n");

		// '{\n'
		XplReadLine(buffer, 4);
		
		readChar = XplReadLine(buffer, XPL_READLN_BUFFER_SIZE);
		if (strncmp_P(buffer, PSTR("command=request"), sizeof("command=request")) != 0)
		{
			return;
		}
			
		// Process config.list request
		return;
	}
	else if (strncmp(buffer, "config.current", sizeof("config.current")) == 0)
	{
		Serial << F("Got a 'config.current' message\n");

		// '{\n'
		XplReadLine(buffer, 4);
		
		readChar = XplReadLine(buffer, XPL_READLN_BUFFER_SIZE);
		if (strncmp_P(buffer, PSTR("command=request"), sizeof("command=request")) != 0)
			return;

		// Process config.current request
		return;
	}
	else if (strncmp(buffer, "config.response", sizeof("config.response")) == 0)
	{
		Serial << F("Got a 'config.response' message\n");

		// '{\n'
		XplReadLine(buffer, 4);
		
		// Process config.response request
		readChar = XplReadLine(buffer, XPL_READLN_BUFFER_SIZE);
		if (strncmp_P(buffer, PSTR("newconf=???"), sizeof("newconf=???")) != 0)
			return;

		return;
	}
	else
	{
		Serial << F("Unknown Message Class: ") << buffer << " : " << readChar << ".\n";
		return;
	}
	
	return;
	

	
#if 0	


//	Serial << F("msgType: ") << msgType_cp << ".\n";
//	Serial << F("hop: ") << hop_cp << ".\n";
//	Serial << F("source: ") << source_cp << ".\n";
//	Serial << F("target: ") << target_cp << ".\n";
//	Serial << F("class: ") << class_cp << ".\n";
//	Serial << F("Command: ") << command_cp << ".\n";
	
	
#endif    
}    // processXplMessage

#if 0
void processXplMessage_(void)
{
    uint16_t packetSize = Udp.available();
    if (! packetSize)
        return;
    
    for (uint8_t i = 50; i; i--) Serial << '-';
//    Serial << '\n' << year() << month() << day() << hour() << minute() << second() << ':' << now();
    Serial << F("\nReceived packet of size ") << packetSize;
    Serial << F("\nFrom ") << Udp.remoteIP() << ':' << Udp.remotePort() << '\n';

    if (packetSize > 255)
        packetSize = 255;

    // Read the packet into packetBuffer
    Udp.read(packetBuffer, packetSize);

    // Terminate packet with \0
    packetBuffer[packetSize] = 0;
//    Serial << F("Contents: ");
//    Serial << (char *)packetBuffer << '\n';
	
	uint8_t msgType = 0;
	uint8_t broadcast = 0;
	uint8_t unicast = 0;
	char * msgType_cp;
	char * hop_cp;
	char * source_cp;
	char * target_cp;
	char * class_cp;
	char * command_cp;
	
	char * cp = (char *) packetBuffer;
	// check for valid xPL message
	// xPL message type is max 8 chars long and terminated with '\n'
	// Valid values are: xpl-cmnd, xpl-stat, xpl-trig
	if (strncmp(cp, "xpl-", 4) != 0)
		return;

	cp += 4;
	if (strncmp(cp, "cmnd", 4) == 0)
	{
		msgType = 'c';
	}
	else if (strncmp(cp, "stat", 4) == 0)
	{
		msgType = 's';
	}
	else if (strncmp(cp, "trig", 4) == 0)
	{
		msgType = 't';
//		return;
	}
	else
		return;
	
	msgType_cp = (char *) packetBuffer;

	cp += 4;
	*cp = 0;

//	Serial << F("msgType: ") << msgType_cp << ".\n";

	// xPL Header: hop
	
	for (; *cp != '='; cp++);
	hop_cp = ++cp;

	for (; *cp != '\n'; cp++);
	*cp = 0;
	
//	Serial << F("hop: ") << hop_cp << ".\n";
	
	// xPL Header: source

	for (; *cp != '='; cp++);
	source_cp = ++cp;

	for (; *cp != '\n'; cp++);
	*cp = 0;

//	Serial << F("source: ") << source_cp << ".\n";

	// xPL Header: target
	
	for (; *cp != '='; cp++);
	target_cp = ++cp;

	for (; *cp != '\n'; cp++);
	*cp = 0;

//	Serial << F("target: ") << target_cp << ".\n";

	// Packet must be either broadcast or unicast
	// Ignore otherwise
	if (strncmp(target_cp, "*", 1) == 0)
	{
		broadcast = 1;
	}
//	else if (strncmp(target_cp, XPL_ENDPOINT, sizeof(XPL_ENDPOINT)) == 0)		// ### hardcoded instance id ... !!!
	else if (strncmp(target_cp, "mhouse.item", sizeof("mhouse.item")) == 0)		// ### hardcoded instance id ... !!!
	{
		unicast = 1;
	}
	else
		return;

		
	// xPL Class
	
	cp += 3;
	class_cp = cp;

	for (; *cp != '\n'; cp++);
	*cp = 0;

//	Serial << F("class: ") << class_cp << ".\n";
	
	/*
		Special classes
		
		hbeat.basic
		hbeat.app
		hbeat.request		<-
		sensor.request
		config.list
		config.current
	*/
	
	// xPL Body: ...
	
	cp += 3;
	
	command_cp = cp;

	for (; *cp != '\n'; cp++);
	*cp = 0;


//	Serial << F("msgType: ") << msgType_cp << ".\n";
//	Serial << F("hop: ") << hop_cp << ".\n";
//	Serial << F("source: ") << source_cp << ".\n";
//	Serial << F("target: ") << target_cp << ".\n";
//	Serial << F("class: ") << class_cp << ".\n";
//	Serial << F("Command: ") << command_cp << ".\n";
	
	if (strncmp(class_cp, "hbeat.request", sizeof("hbeat.request")) == 0)
	{
		Serial << F("Got a 'hbeat.request' message.\n");
		
		if (strncmp(command_cp, "command=request", sizeof("command=request")) != 0)
			return;

		Serial << F("Go to send a heartbeat message.\n");
			
		sendXplHeartbeat();
		return;		
	}
	else if (strncmp(class_cp, "config.list", sizeof("config.list")) == 0)
	{
		Serial << F("Got a 'config.list' message\n");

		if (strncmp(cp, "command=request", sizeof("command=request")) != 0)
			return;
			
		// Process config.list request
	}
	else if (strncmp(class_cp, "config.current", sizeof("config.current")) == 0)
	{
		Serial << F("Got a 'config.current' message\n");

		if (strncmp(cp, "command=request", sizeof("command=request")) != 0)
			return;

		

	}
	else
		return;
	
    
}    // processXplMessage
#endif


/*
	sendXplS0ConfigMsg
*/
void sendXplS0ConfigMsg(S0Impulse *S0)
{
//    Serial << year() << month() << day() << hour() << minute() << second() << ':' << now() << '\n';
    Serial << F("Send xPL S0 Update for ") << S0->getID() << '\n';
	
    Udp.beginPacket(xPLBroadcast, xPLPort);

    Udp << F("xpl-stat\n"
        "{\n"
            "hop=1\n"
            "source=" XPL_ENDPOINT "\n"
            "target=*\n"
        "}\n"
        "config.list\n"
        "{\n"
			"reconf=newconf\n"
            "device=") << S0->getID() << F("\n"
            "type=energy\n"
            "current=");
//	Udp.print(S0->getCount() );
	Udp << F("\n"
			"units=kWh\n"
        "}\n"
    );

    Udp.endPacket();    
}    // sendXplS0ConfigMsg

/*
	sendXplS0TriggerMsg
*/
void sendXplS0TriggerMsg(S0Impulse *S0)
{
//    Serial << year() << month() << day() << hour() << minute() << second() << ':' << now() << '\n';
    Serial << F("Send xPL S0 Update for ") << S0->getID() << '\n';
	
    Udp.beginPacket(xPLBroadcast, xPLPort);

    Udp << F("xpl-trig\n"
        "{\n"
            "hop=1\n"
            "source=" XPL_ENDPOINT "\n"
            "target=*\n"
        "}\n"
        "sensor.basic\n"
        "{\n"
            "device=") << S0->getID() << F("\n"
            "type=energy\n"
            "current=");
	Udp.print(S0->getCount(), 3);
	Udp << F("\n"
			"units=kWh\n"
        "}\n"
    );

    Udp.endPacket();    
}    // sendXplS0TriggerMsg


void sendXplHeartbeat(void)
{
//    Serial << year() << month() << day() << hour() << minute() << second() << ':' << now() << '\n';
    Serial << F("Send xPL Heartbeat\n");

    Udp.beginPacket(xPLBroadcast, xPLPort);
    Udp << F(
		"xpl-stat\n"
        "{\n"
            "hop=1\n"
            "source=" XPL_ENDPOINT "\n"
            "target=*\n"
        "}\n"
		"hbeat.basic\n"
        "{\n"
            "interval=1\n"
            "version=" FIRMWARE_VERSION "\n"
            "source=" XPL_ENDPOINT "\n"
#if 0
            "s0_0_lph=") << S0[0].getCountLowPerHigh() << F("\n"
            "s0_1_lph=") << S0[1].getCountLowPerHigh() << F("\n"
            "s0_2_lph=") << S0[2].getCountLowPerHigh() << F("\n"
            "s0_3_lph=") << S0[3].getCountLowPerHigh() << F("\n"
            "s0_4_lph=") << S0[4].getCountLowPerHigh() << F("\n"
            "s0_5_lph=") << S0[5].getCountLowPerHigh() << F("\n"
            "s0_6_lph=") << S0[6].getCountLowPerHigh() << F("\n"
            "s0_7_lph=") << S0[7].getCountLowPerHigh() << F("\n"
#endif
        "}\n"
    );

    Udp.endPacket();
}    // sendXplHeartbeat
#endif

#if 1		// EEPROM related functions
/*---------------------------------------------------------------------
	initEEPROM
	
	Initialize EEPROM with some reasonable values
---------------------------------------------------------------------*/
void initEEPROM(uint8_t value, uint16_t from, uint16_t count)
{
	uint8_t i;

	Serial << F("initEEPROM with ") << value << F(" from ") << from << F(" to ") << from + count;
	
	for (i = from; i < from + count; i++)
		eeprom_write_byte((uint8_t *) i, value);

	Serial << F(" ... done.\n");
		
}	// initEEPROM

/*---------------------------------------------------------------------
	dumpEEPROM
	
	Dump EEPROM values
---------------------------------------------------------------------*/
void dumpEEPROM(uint16_t from, uint16_t count)
{
	char buf[16];
	
	Serial << F("Dump EEPROM contents from ") << from << F(" to ") << from + count << '(' << count << F(")bytes\n");
	snprintf(buf, 15, "%04d %03X: ", 0, 0);
	Serial << buf;

	uint16_t i = from;
	uint8_t j = 1;
	while (i < from + count)
	{
		snprintf(buf, 15, "%02X ", eeprom_read_byte((uint8_t *)i));
		Serial << buf;

		i++;	// advance here for the print below

		if (j < 16)
			Serial << ' ';
		else
		{
			if (i < from + count)		// skip the last empty preamble of the line as while would end anyway
			{
				j = 0;
				snprintf(buf, 15, "\n%04d %03X: ", i, i);
				Serial << buf;
			}
		}
		j++;
	}
	Serial << '\n';
}	// dumpEEPROM
#endif

#if 1		// Various stuff - 
/**********************************************************************
	freeRam
	
	Taken from 
	http://www.controllerprojects.com/2011/05/23/determining-sram-usage-on-arduino/
	---
	As stated on the JeeLabs site:

		There are three areas in RAM:

		* static data, i.e. global variables and arrays … and strings !
		* the “heap”, which gets used if you call malloc() and free()
		* the “stack”, which is what gets consumed as one function calls 
			another

		The heap grows up, and is used in a fairly unpredictable manner. 
		If you release areas, then they will be lead to unused gaps in 
		the heap, which get re-used by new calls to malloc() if the 
		requested block fits in those gaps.

		At any point in time, there is a highest point in RAM occupied 
		by the heap. This value can be found in a system variable 
		called __brkval.

		The stack is located at the end of RAM, and expands and 
		contracts down towards the heap area. Stack space gets 
		allocated and released as needed by functions calling other 
		functions. That’s where local variables get stored.
	---
	
	IMO int v; sits at the lowest address of the stack currently
	
**********************************************************************/
int freeRam(void) 
{
  extern int __heap_start, *__brkval; 
  int v;
  
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}	// freeRam
#endif
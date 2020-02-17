#include <WiFi.h>

// #define FASTLED_ESP32_I2S
// #define FASTLED_ALLOW_INTERRUPTS
#include <FastLED.h>

// #define USE_AP				// The driver start has a WiFi Acess point
// #define USE_WIFI				// The driver use WIFI_SSID and WIFI_PASSWORD
#define USE_WIFI_MANAGER		// The driver use Wifi manager
// #define USE_RESET_BUTTON		// Can reset Wifi manager with button

// #define USE_OTA				// Activate Over the Air Update
#define USE_ANIM				// activate animation in SPI filesysteme (need BROTLI)
#define USE_FTP					// activate FTP server (need USE ANIM)
// #define USE_8_OUTPUT			// active 8 LEDs output

#define USE_UDP
#define USE_BROTLI
#define USE_ZLIB

#define PRINT_FPS
// #define PRINT_DEBUG
// #define PRINT_DMX
// #define PRINT_RLE

#ifdef USE_WIFI_MANAGER
	#include <DNSServer.h>
	#include <WebServer.h>
	#include <WiFiManager.h>
#endif

#ifdef USE_ANIM
	#include "SPIFFS.h"
#endif

#ifdef USE_FTP
	#include <ESP8266FtpServer.h>
#endif

#ifdef USE_OTA
	#include <ArduinoOTA.h>
#endif

#ifdef USE_UDP
	#include <Artnet.h>
	#include <AsyncUDP_big.h>
#endif

#if defined(USE_ZLIB)
	#include <miniz.c>
#endif

#if defined(USE_BROTLI) || defined(USE_ANIM)
	#include <brotli/decode.h>
#endif

#ifdef PRINT_FPS
	#include <SimpleTimer.h>
#endif

#define FIRMWARE_VERSION	"1.0"

#define DEFAULT_HOSTNAME	"ESP32_LEDs"
#define AP_PASSWORD			"WIFI_PASSWORD"

#define WIFI_SSID			""
#define WIFI_PASSWORD		""

#define FTP_USER			"LED"
#define FTP_PASS			"LED"

#define ART_NET_PORT	6454
#define UDP_PORT		ART_NET_PORT
#define OTA_PORT		8266

#define LED_TYPE		WS2812B
#define COLOR_ORDER		GRB
#define BRIGHTNESS		255

#define START_UNI		0
#define UNI_BY_STRIP	2
#ifdef USE_8_OUTPUT
	#define NUM_STRIPS	8
#else
	#define NUM_STRIPS	1
#endif
#define LEDS_BY_UNI		170
#define LED_BY_STRIP	512 //(UNI_BY_STRIP*LEDS_BY_UNI)
#define LED_TOTAL		(LED_BY_STRIP*NUM_STRIPS)
#define LED_VCC			5	// 5V
#define LED_MAX_CURRENT	500	// 2000mA

#define RESET_WIFI_PIN	23

#define LED_PORT_0		13 // 16
#define LED_PORT_1		4
#define LED_PORT_2		2
#define LED_PORT_3		22
#define LED_PORT_4		19
#define LED_PORT_5		18
#define LED_PORT_6		21
#define LED_PORT_7		17

enum UDP_PACKET {
	LED_RGB_888 = 0,
	LED_RGB_888_UPDATE = 1,

	LED_RGB_565 = 2,
	LED_RGB_565_UPDATE = 3,

	LED_UPDATE = 4,
	GET_INFO = 5,

	LED_TEST = 6,
	LED_RGB_SET = 7,
	LED_LERP = 8,

	SET_MODE = 9,
	REBOOT = 10,

	LED_RLE_888 = 11,
	LED_RLE_888_UPDATE = 12,

	LED_BRO_888 = 13,
	LED_BRO_888_UPDATE = 14,

	LED_Z_888 = 15,
	LED_Z_888_UPDATE = 16,

};

enum ANIM {
	ANIM_UDP = 0,
	ANIM_START = 1,
	ANIM_PLAY = 2
};

#ifdef USE_UDP
	AsyncUDP_big	udp;
	Artnet			artnet;
#endif

uint8_t			led_state = 0;
uint16_t		paquet_count = 0;

#ifdef USE_WIFI_MANAGER
	WiFiManager	wifiManager;
#endif

#ifdef USE_FTP
	FtpServer ftpSrv;
#endif

#ifdef USE_ANIM
	File root;
	File file;

	uint8_t buff[2000];
	uint8_t head[5];

	uint16_t fps;
	uint16_t nb;
	uint16_t wait;
	uint16_t compress_size;
	size_t un_size = sizeof(buff);

	unsigned long previousMillis = 0;
	uint8_t		anim = ANIM_START;
#else
	uint8_t		anim = ANIM_UDP;
#endif

char 		hostname[50] = DEFAULT_HOSTNAME;
char 		firmware[20] = FIRMWARE_VERSION;

CRGB		leds[LED_TOTAL];

int			Pins[8] = {LED_PORT_0,LED_PORT_1,LED_PORT_2,LED_PORT_3,LED_PORT_4,LED_PORT_5,LED_PORT_6,LED_PORT_7};

#ifdef PRINT_FPS
	SimpleTimer	timer;
#endif
unsigned long frameCounter = 0;
unsigned long frameLastCounter = frameCounter;

#ifdef PRINT_FPS
	void timerCallback(){
		if (frameLastCounter != frameCounter) {
			Serial.printf("FPS: %lu Frames received: %lu\n", frameCounter-frameLastCounter, frameCounter);
			frameLastCounter = frameCounter;
		}
	}
#endif

#ifdef USE_WIFI_MANAGER
	//flag for saving data
	bool shouldSaveConfig = false;

	//callback notifying us of the need to save config
	void saveConfigCallback () {
		Serial.println("Should save config");
		shouldSaveConfig = true;
	}
#endif

#ifdef USE_UDP

	void onDmxFrame(uint16_t universe, uint16_t length, uint8_t sequence, uint8_t* data, IPAddress remoteIP) {

		static uint8_t dmx_counter = 0;

		#ifdef PRINT_DMX
			Serial.print("DMX: Univ: ");
			Serial.print(universe, DEC);
			Serial.print(", Seq: ");
			Serial.println(sequence, DEC);
		#endif

		if (sequence != dmx_counter + 1 && sequence) {
			// Serial.println("LOST frame: ");
		}
		paquet_count++;

		if (sequence == 255) {
			Serial.printf("%d / 256 = %02f%%\n", paquet_count, paquet_count/256.0 * 100.0 );
			paquet_count = 0;
		}

		dmx_counter = sequence;

		if (length<3 || universe < START_UNI || universe >  (START_UNI + UNI_BY_STRIP * NUM_STRIPS))
			return;

		uint16_t off  = (universe - START_UNI) * LEDS_BY_UNI * 3;

		memcpy(((uint8_t*)leds) + off, data, LEDS_BY_UNI*3);
		// if (universe==3)
			// LEDS.show();
	}

	void onSync(IPAddress remoteIP) {
		#ifdef PRINT_DMX
			Serial.println("DMX: Sync");
		#endif
		LEDS.show();
	}

	void udp_receive(AsyncUDP_bigPacket packet) {
	int len = packet.length();
	if (len) {
		#ifdef PRINT_DEBUG
			Serial.printf("Received %d bytes from %s, port %d\n", len, packet.remoteIP().toString().c_str(), packet.remotePort());
		#endif
		uint8_t		*pqt		= packet.data();
		uint8_t 	type		= pqt[0];
		uint8_t		seq			= pqt[1];
		uint16_t	leds_off	= *(uint16_t *)(pqt+2);
		uint16_t	leds_nb		= *(uint16_t *)(pqt+4);
		uint8_t		*data		= (uint8_t*)&pqt[6];
		uint16_t	*data16		= (uint16_t*)&pqt[6];
		size_t		un_size		= LED_TOTAL*3;

		anim = ANIM_UDP;

		if (type != 'A') {
			paquet_count++;
			if (seq == 255) {
				Serial.printf("%d / 256 = %02f%%\n", paquet_count, paquet_count/256.0 * 100.0 );
				paquet_count = 0;
			}
		}
		#ifdef PRINT_DEBUG
			Serial.printf("receive: type:%d, seq:%d, off:%d, nb:%d\n", type, seq, leds_off, leds_nb);
		#endif
		switch (type) {
			case 'A': // Art-Net
				artnet.read(&packet);
				break;
			case LED_RGB_888:
			case LED_RGB_888_UPDATE:
				if (len - 6 == (leds_nb*3))
					memcpy(((uint8_t*)leds)+leds_off*3, data, leds_nb*3);
				else
					Serial.println("Invalid LED_RGB_888_UPDATE len");
				if (type == LED_RGB_888_UPDATE) {
					LEDS.show();
					frameCounter++;
				}
				break;
			case LED_RGB_565:
			case LED_RGB_565_UPDATE:
				if (len - 6 == (leds_nb*2)) {
					for (uint16_t i=0; i<leds_nb; i++) {
						uint8_t r = ((((data16[i] >> 11) & 0x1F) * 527) + 23) >> 6;
						uint8_t g = ((((data16[i] >> 5) & 0x3F) * 259) + 33) >> 6;
						uint8_t b = (((data16[i] & 0x1F) * 527) + 23) >> 6;
						leds[i] = r << 16 | g << 8 | b;
					}
				}
				else
					Serial.println("Invalid LED_RGB_565 len");
				if (type == LED_RGB_565_UPDATE) {
					LEDS.show();
					frameCounter++;
				}
			case LED_UPDATE:
				LEDS.show();
				frameCounter++;
				break;
			case REBOOT:
				Serial.println("Receive REBOOT");
				ESP.restart();
				break;
			case LED_RLE_888:
			case LED_RLE_888_UPDATE:
				for (int i=0; i<(len-6); i+=4) {
					#ifdef PRINT_RLE
						Serial.printf("  nb:%d {%02d, %02d, %02d}\n", data[i],data[i+1],data[i+2],data[i+3]);
					#endif
					for (int j=0;j<data[i]; j++) {
						// Serial.printf("  off:%d {%02d}\n", leds_off, data[i+2]);
						memcpy(leds+leds_off, data+i+1, 3);
						leds_off++;
					}
				}
				if (type == LED_RLE_888_UPDATE) {
					LEDS.show();
					frameCounter++;
				}
				break;
			#ifdef USE_BROTLI
			case LED_BRO_888_UPDATE:
				BrotliDecoderDecompress( // need xTaskCreateUniversal(_udp_task, "async_udp", 8160,
					len-6,
					(const uint8_t *)data,
					&un_size,
					(uint8_t*)leds
				);
				LEDS.show();
				frameCounter++;
				break;
			#endif
			#ifdef USE_ZLIB
			case LED_Z_888_UPDATE:
				uncompress(
					(uint8_t*)(leds+leds_off),
					(long unsigned int*)&un_size,
					(const uint8_t *)data,
					len-6
				);
				LEDS.show();
				frameCounter++;
				break;
			#endif
			default:
				Serial.printf("UDP packet type unknown: %d\n", type);
				break;
		}
	}
}

#endif

void setup() {
	Serial.begin(115200);

	#if defined(USE_WIFI_MANAGER) && defined(USE_RESET_BUTTON)
		pinMode(RESET_WIFI_PIN, INPUT);
	#endif

	Serial.println("\n------------------------------");
	Serial.printf("  LEDs driver V%s\n", FIRMWARE_VERSION);
	Serial.printf("  Hostname: %s\n", hostname);
	int core = xPortGetCoreID();
	Serial.print("  Main code running on core ");
	Serial.println(core);
	Serial.println("------------------------------");

	#ifdef USE_8_OUTPUT
		LEDS.addLeds<LED_TYPE,LED_PORT_0,COLOR_ORDER>(leds, 0 * LED_BY_STRIP, LED_BY_STRIP).setCorrection(TypicalLEDStrip);
		LEDS.addLeds<LED_TYPE,LED_PORT_1,COLOR_ORDER>(leds, 1 * LED_BY_STRIP, LED_BY_STRIP).setCorrection(TypicalLEDStrip);
		LEDS.addLeds<LED_TYPE,LED_PORT_2,COLOR_ORDER>(leds, 2 * LED_BY_STRIP, LED_BY_STRIP).setCorrection(TypicalLEDStrip);
		LEDS.addLeds<LED_TYPE,LED_PORT_3,COLOR_ORDER>(leds, 3 * LED_BY_STRIP, LED_BY_STRIP).setCorrection(TypicalLEDStrip);
		LEDS.addLeds<LED_TYPE,LED_PORT_4,COLOR_ORDER>(leds, 4 * LED_BY_STRIP, LED_BY_STRIP).setCorrection(TypicalLEDStrip);
		LEDS.addLeds<LED_TYPE,LED_PORT_5,COLOR_ORDER>(leds, 5 * LED_BY_STRIP, LED_BY_STRIP).setCorrection(TypicalLEDStrip);
		LEDS.addLeds<LED_TYPE,LED_PORT_6,COLOR_ORDER>(leds, 6 * LED_BY_STRIP, LED_BY_STRIP).setCorrection(TypicalLEDStrip);
		LEDS.addLeds<LED_TYPE,LED_PORT_7,COLOR_ORDER>(leds, 7 * LED_BY_STRIP, LED_BY_STRIP).setCorrection(TypicalLEDStrip);
	#else
		LEDS.addLeds<LED_TYPE,LED_PORT_0,COLOR_ORDER>(leds, 0 * LED_BY_STRIP, LED_BY_STRIP).setCorrection(TypicalLEDStrip);
	#endif
	LEDS.setBrightness(BRIGHTNESS);
	#ifdef LED_MAX_CURRENT
		LEDS.setMaxPowerInVoltsAndMilliamps(LED_VCC, LED_MAX_CURRENT);
	#endif
	Serial.println("LEDs driver start");

	#if defined(USE_WIFI_MANAGER)
		wifiManager.setTimeout(180);
		wifiManager.setConfigPortalTimeout(180); // try for 3 minute
		wifiManager.setMinimumSignalQuality(15);
		wifiManager.setRemoveDuplicateAPs(true);
		wifiManager.setSaveConfigCallback(saveConfigCallback);
		wifiManager.autoConnect("ESP32_LEDs");
		Serial.println("Wifi Manager start");

	#elif defined(USE_AP)
		Serial.println("Setting AP (Access Point)");
		WiFi.softAP("ESP32_LEDs_AP", AP_PASSWORD);
		IPAddress IP = WiFi.softAPIP();
		Serial.print("AP IP address: ");
		Serial.println(IP);

	#elif defined(USE_WIFI)
		WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
		uint8_t retryCounter = 0;
		while (WiFi.status() != WL_CONNECTED) {
			delay(1000);
			Serial.println("Establishing connection to WiFi..");
			retryCounter++;
			if (retryCounter>5) {
				Serial.println("Could not connect, restarting");
				delay(10);
				ESP.restart();
			}
		}
	#endif

	Serial.print("Connected to:\t");
	Serial.println(WiFi.SSID());
	Serial.print("IP address:\t");
	Serial.println(WiFi.localIP());
	WiFi.setSleep(false);

	#ifdef USE_UDP
		if(udp.listen(UDP_PORT)) {
			Serial.printf("UDP server started on port %d\n", UDP_PORT);
			udp.onPacket(udp_receive);

			artnet.begin();
			artnet.setArtDmxCallback(onDmxFrame);
			artnet.setArtSyncCallback(onSync);

			#ifdef USE_AP
				artnet.setIp(WiFi.softAPIP());
			#endif
		}
	#endif

	#ifdef USE_OTA
		ArduinoOTA.setPort(OTA_PORT);
		ArduinoOTA.setHostname(hostname);

		ArduinoOTA.onStart([]() {
			String type;
			if (ArduinoOTA.getCommand() == U_FLASH)
				type = "sketch";
			else // U_SPIFFS
				type = "filesystem";

			// NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
			Serial.println("Start updating " + type);
		});

		ArduinoOTA.onEnd([]() {
			Serial.println("\nEnd");
		});

		ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
			Serial.printf("Progress: %u%%\r\n", (progress / (total / 100)));
			led_state+=1;
		});

		ArduinoOTA.onError([](ota_error_t error) {
			Serial.printf("Error[%u]: ", error);
			if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
			else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
			else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
			else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
			else if (error == OTA_END_ERROR) Serial.println("End Failed");
			ESP.restart();
		});

		ArduinoOTA.begin();
		Serial.printf("OTA server started on port %d\n", OTA_PORT);
	#endif

	#ifdef USE_ANIM
		if(!SPIFFS.begin(true)){
			Serial.println("An Error has occurred while mounting SPIFFS");
			return;
		}
		Serial.println("mounting SPIFFS OK");

		root = SPIFFS.open("/");
		file = root.openNextFile();
	#endif

	#ifdef USE_FTP
		ftpSrv.begin(FTP_USER, FTP_PASS);
		Serial.println("FTP Server Start");
	#endif

	#ifdef PRINT_FPS
		timer.setTimer(1000, timerCallback, 6000); // Interval to measure FPS  (millis, function called, times invoked for 1000ms around 1 hr and half)
	#endif
}

#ifdef USE_ANIM
	void load_anim() {
		Serial.print("  FILE: ");
		Serial.print(file.name());
		Serial.print("\tSIZE: ");
		Serial.println(file.size());

		Serial.println("Open animation");

		file.read(head, 5);

		fps				= (head[2] << 8) | head[1];
		nb 				= (head[4] << 8) | head[3];
		wait			= 1000.0 / fps;
		compress_size	= 0;

		Serial.printf("  FPS: %02d; LEDs: %04d format: %d\n", fps, nb, head[0]);
		anim = ANIM_PLAY;
	}

	void read_anim_frame() {
		file.read((uint8_t*)&compress_size, 2);
		file.read(buff, compress_size);
		BrotliDecoderDecompress(
			compress_size,
			(const uint8_t *)buff,
			&un_size,
			(uint8_t*)leds
		);
		LEDS.show();
		if (!file.available()) {
			anim = ANIM_START;
			file.close();
			file = root.openNextFile();
			if (!file) {
				root.close();
				root = SPIFFS.open("/");
				file = root.openNextFile();
			}
		}
	}
#endif

void loop(void) {
	#ifdef PRINT_FPS
		timer.run();
	#endif

	#ifdef USE_OTA
		ArduinoOTA.handle();
	#endif

	#ifdef USE_FTP
		ftpSrv.handleFTP();
	#endif

	#if defined(USE_WIFI_MANAGER) && defined(USE_RESET_BUTTON)
		if (!digitalRead(RESET_WIFI_PIN)) {
			Serial.println("Reset Wifi");
			wifiManager.startConfigPortal("ESP32_LEDs");
			delay(500);
			ESP.restart();
		}
	#endif

	#ifdef USE_ANIM
		switch (anim) {
			case ANIM_START:
				load_anim();
				break;
			case ANIM_PLAY:
				if (millis() - previousMillis >= wait) {
					previousMillis = millis();
					read_anim_frame();
				}
				break;
			case ANIM_UDP:
				break;
		}
	#endif
}

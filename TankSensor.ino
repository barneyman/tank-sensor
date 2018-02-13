/*
 Name:		TankSensor.ino
 Created:	1/3/2018 5:14:48 PM
 Author:	bflint
*/


#include <SPI.h>
#include <ESP8266HTTPClient.h>
#include <hardwareSerial.h>
#include <ESP8266httpUpdate.h>
#include <WiFiClient.h>

#include <ArduinoJson.h>

#define _MYVERSION	"tank_1.9"

#define _JSON_CONFIG_FILE "CONFIG.JSN"

#define _JSON_DATA_FILE			"DATA.JSN"
#define _JSON_DATA_SEPARATOR	'\n'

#define _MAX_JSON_DATA_SIZE_GB	1
#define _MAX_JSON_DATA_SIZE		(_MAX_JSON_DATA_SIZE_GB*(1024*1024*1024))

#define _JSON_BODY_LEN 500


#define _SPIFF_RESET_FLAG_FILE	"/reset.txt"

#include <SD.h>

#define FS_NO_GLOBALS	// turn off the 'Using'
#include <FS.h>

// enable this define for deepsleep (RST and D0 must be connected)
#define _SLEEP_PERCHANCE_TO_DREAM

#define _AP_SLEEP_AFTER_MS(a)	a
#define _AP_SLEEP_AFTER_S(a)	_AP_SLEEP_AFTER_MS(1000*a)
#define _AP_SLEEP_AFTER_M(a)	_AP_SLEEP_AFTER_S(60*a)
#define _AP_SLEEP_AFTER_H(a)	_AP_SLEEP_AFTER_M(60*a)

#ifdef _SLEEP_PERCHANCE_TO_DREAM
// default sample period - 15 mins
#define _SAMPLE_INTERVAL_M	(15)
#define _AP_SLEEP_TIMEOUT			_AP_SLEEP_AFTER_M(2)
#define _AP_SLEEP_TIMEOUT_STAAP		_AP_SLEEP_AFTER_S(20)
#define _AP_SLEEP_TIMEOUT_AP		_AP_SLEEP_TIMEOUT
#define _AP_SLEEP_TIMEOUT_FOREVER	_AP_SLEEP_AFTER_H(1)
#else
#define _SAMPLE_INTERVAL_M			(5)
#define _AP_SLEEP_TIMEOUT			_AP_SLEEP_AFTER_M(3)
#define _AP_SLEEP_TIMEOUT_STAAP		_AP_SLEEP_AFTER_S(20)
#define _AP_SLEEP_TIMEOUT_AP		_AP_SLEEP_TIMEOUT
#define _AP_SLEEP_TIMEOUT_FOREVER	-1
#endif


//#define _TRY_PING
#include <ESP8266Ping.h>

#include <Wire.h>


// my libs
#include <myWifi.h>
myWifiClass wifiInstance("wemos_");



#include <BME280I2C.h>

BME280I2C::Settings mySettings = {

	/*OSR _tosr =			*/BME280::OSR_X16,
	/*OSR _hosr =			*/BME280::OSR_X1,
	/*OSR _posr =			*/BME280::OSR_X1,
	/*Mode _mode =			*/BME280::Mode_Forced,
	/*StandbyTime _st =		*/BME280::StandbyTime_1000ms,
	/*Filter _filter =		*/BME280::Filter_16,
	/*SpiEnable _se =		*/BME280::SpiEnable_False,
	/*I2CAddr _addr =		*/BME280I2C::I2CAddr_0x76


};

BME280I2C bme(mySettings);


struct {
	unsigned version;
	unsigned long iteration, iterationSent;
	unsigned long samplePeriodMins;

	// wifi deets
	myWifiClass::wifiDetails wifi;

	IPAddress postHost;
	unsigned postHostPort;

} config =
{
	0,					// ver
	0,0,				// iters
	_SAMPLE_INTERVAL_M,
	{
		"","",false,true,IPAddress(),IPAddress(),IPAddress()
	},
	IPAddress(),5000

};





#ifdef _SLEEP_PERCHANCE_TO_DREAM
unsigned long millisAtBoot = millis();
#endif
unsigned long lastSeenTraffic = 0;
//#define _CLEAR_DATA

// NOT a mistake - D* is normally slave select for SPI, but i only have one 
// SPI device, so i'm slaving that to ground and reusing the pin 
// because i cant use D3/D4 (gpio 0 and 2)
#define TRAN_PIN	D8

// the setup function runs once when you press reset or power the board
void setup() {



	Serial.begin(115200);
	///Serial.setTimeout(2000);
	pinMode(TRAN_PIN, OUTPUT);
	digitalWrite(TRAN_PIN, LOW);

	DEBUG(DEBUG_IMPORTANT, Serial.printf("Awake. version %s\n\r",_MYVERSION));
	digitalWrite(TRAN_PIN, HIGH);


	// see comment for TRAN_PIN
	if (!SD.begin(D4))
	{
		DEBUG(DEBUG_ERROR,Serial.println("SD card failed"));
	}

	if(!SPIFFS.begin())
	{
		DEBUG(DEBUG_ERROR, Serial.println("SPIFFS failed"));
	}

	// special case
	if (SPIFFS.exists(_SPIFF_RESET_FLAG_FILE))
	{
		DEBUG(DEBUG_IMPORTANT, Serial.println("Resetting"));
		SD.remove(_JSON_DATA_FILE);
		if (
			SD.remove(_JSON_CONFIG_FILE) )
		{
			SPIFFS.remove(_SPIFF_RESET_FLAG_FILE);
			DEBUG(DEBUG_IMPORTANT, Serial.println("Deleting config - reset.txt"));
		}
		else
		{
			DEBUG(DEBUG_ERROR, Serial.println("FAILED Deleting config- reset.txt"));
		}
	}



	readConfig();

	// if we haven't been configured, wake up and offer a webserver to configure us
	if (!config.wifi.configured)
	{
		wifiInstance.ConnectWifi(myWifiClass::modeAP, config.wifi);

		// begin the servers
		DEBUG(DEBUG_VERBOSE, Serial.println("serving static pages"));

		wifiInstance.server.on("/", HTTP_GET, []() {

			lastSeenTraffic = millis();

			fs::File f;
			if(wifiInstance.currentMode!=myWifiClass::wifiMode::modeSTAandAP)
				f = SPIFFS.open("/APmode.htm", "r");
			else
				f = SPIFFS.open("/STAAPmode.htm", "r");
			wifiInstance.server.streamFile(f, "text/html");
			f.close();

		});

		wifiInstance.server.on("/stopAP", []() {

			DEBUG(DEBUG_VERBOSE, Serial.println("/stopAP"));
			lastSeenTraffic = millis();

			if (wifiInstance.currentMode == myWifiClass::wifiMode::modeSTAandAP)
			{
				wifiInstance.ConnectWifi(myWifiClass::wifiMode::modeSTA, config.wifi);
				wifiInstance.server.send(200, "text/html", "<html/>");
			}
			else
			{
				wifiInstance.server.send(500, "text/html", "<html/>");
			}
		});

		wifiInstance.server.on("/json/config", HTTP_GET, []() {
			// give them back the port / switch map

			DEBUG(DEBUG_INFO, Serial.println("json config called"));
			lastSeenTraffic = millis();

			DynamicJsonBuffer jsonBuffer;

			JsonObject &root = jsonBuffer.createObject();

			root["name"] = wifiInstance.m_hostName.c_str();

			String jsonText;
			root.prettyPrintTo(jsonText);

			DEBUG(DEBUG_VERBOSE, Serial.println(jsonText));

			wifiInstance.server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
			wifiInstance.server.send(200, "application/json", jsonText);
		});

		wifiInstance.server.on("/json/wificonfig", HTTP_GET, []() {
			// give them back the port / switch map

			DEBUG(DEBUG_INFO, Serial.println("json wificonfig called"));
			lastSeenTraffic = millis();

			DynamicJsonBuffer jsonBuffer;

			JsonObject &root = jsonBuffer.createObject();

			root["name"] = wifiInstance.m_hostName.c_str();
			root["ssid"] = wifiInstance.SSID();
			root["ip"] = wifiInstance.localIP().toString();

			String jsonText;
			root.prettyPrintTo(jsonText);

			DEBUG(DEBUG_VERBOSE, Serial.println(jsonText));

			wifiInstance.server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
			wifiInstance.server.send(200, "application/json", jsonText);
		});


		wifiInstance.server.on("/json/wifi", HTTP_GET, []() {
			// give them back the port / switch map

			lastSeenTraffic = millis();
			DEBUG(DEBUG_INFO, Serial.println("json wifi called"));

			DynamicJsonBuffer jsonBuffer;

			JsonObject &root = jsonBuffer.createObject();
			root["name"] = wifiInstance.m_hostName.c_str();
			JsonArray &wifis = root.createNestedArray("wifi");

			// let's get all wifis we can see
			std::vector<std::pair<String, int>> allWifis;
			int found = wifiInstance.ScanNetworks(allWifis);

			int maxFound = found < 10 ? found : 10;

			for (int each = 0; each < maxFound; each++)
			{
				JsonObject &wifi = wifis.createNestedObject();
				wifi["ssid"] = allWifis[each].first;
				wifi["sig"] = allWifis[each].second;

				DEBUG(DEBUG_INFO, Serial.printf("%d '%s' %d \n\r", each + 1, allWifis[each].first.c_str(), allWifis[each].second));

			}

			root["hostport"] = config.postHostPort;
			root["period"] = config.samplePeriodMins;


			String jsonText;
			root.prettyPrintTo(jsonText);

			DEBUG(DEBUG_VERBOSE, Serial.println(jsonText));

			// do not cache
			wifiInstance.server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
			wifiInstance.server.send(200, "text/json", jsonText);
		});

		wifiInstance.server.on("/json/wifi", HTTP_POST, []() {

			lastSeenTraffic = millis();
			DEBUG(DEBUG_INFO, Serial.println("json wifi posted"));
			DEBUG(DEBUG_INFO, Serial.println(wifiInstance.server.arg("plain")));

			DynamicJsonBuffer jsonBuffer;
			// 'plain' is the secret source to get to the body
			JsonObject& root = jsonBuffer.parseObject(wifiInstance.server.arg("plain"));

			String ssid = root["ssid"];
			String pwd = root["pwd"];

			// sanity check these values

			config.wifi.ssid = ssid;
			config.wifi.password = pwd;

			// dhcp or static?
			if (root["dhcp"] == 1)
			{
				config.wifi.dhcp = true;
			}
			else
			{
				config.wifi.dhcp = false;
				config.wifi.ip.fromString((const char*)root["ip"]);
				config.wifi.gateway.fromString((const char*)root["gateway"]);
				config.wifi.netmask.fromString((const char*)root["netmask"]);
			}

			config.postHost.fromString((const char*)root["loghost"]);
			config.postHostPort = root["loghostport"];
			config.samplePeriodMins = root["loghostperiod"];

			// force attempt
			// if we fail we fall back to AP
			if (wifiInstance.ConnectWifi(myWifiClass::wifiMode::modeSTAspeculative, config.wifi) == myWifiClass::wifiMode::modeSTAandAP)
			{
#ifdef _PING_CAN_BE_TOLD_WHICH_ITF_TO_USE
				// IIF we can ping the host, we accept this
				DEBUG(DEBUG_VERBOSE, Serial.printf("confirming a ping to %s\n\r", config.postHost.toString().c_str()));
				if (Ping.ping(config.postHost,3))
				{
					DEBUG(DEBUG_IMPORTANT, Serial.println("PING success"));
					config.wifi.configured = true;
				}
				else
				{
					DEBUG(DEBUG_IMPORTANT, Serial.println("PING FAILED, reverting"));
					config.wifi.configured = false;
					wifiInstance.ConnectWifi(myWifiClass::wifiMode::modeAP, config.wifi);
				}
#else
				config.wifi.configured = true;
#endif
			}
			else
			{
				DEBUG(DEBUG_IMPORTANT, Serial.println("Speculative join failed"));
				config.wifi.configured = false;
			}

			// and update json
			writeConfig();

			wifiInstance.server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
			wifiInstance.server.send(200, "text/json", "<html/>");

		});



		// serve up everthing in SPIFFS
		SPIFFS.openDir("/");

		fs::Dir dir = SPIFFS.openDir("/");
		while (dir.next()) {
			String file = dir.fileName();

			// cache it for an hour
			wifiInstance.server.serveStatic(file.c_str(), SPIFFS, file.c_str(), "Cache-Control: public, max-age=60");

			DEBUG(DEBUG_VERBOSE, Serial.printf("Serving %s\n\r", file.c_str()));

		}
	}
	else
	{ 
		wifiInstance.ConnectWifi(myWifiClass::modeSTA, config.wifi, false);
	}

	Wire.begin(D2, D1);
	bme.begin();


	switch (bme.chipModel())
	{
	case BME280::ChipModel_BME280:
		Serial.println("Found BME280 sensor! Success.");
		break;
	case BME280::ChipModel_BMP280:
		Serial.println("Found BMP280 sensor! No Humidity available.");
		break;
	default:
		Serial.println("Found UNKNOWN sensor! Error!");
	}

	lastSeenTraffic = millis();

}




bool readConfig()
{
	DEBUG(DEBUG_VERBOSE, Serial.println("reading config"));

	// try to read the config - if it fails, create the default
	File configFile=SD.open(_JSON_CONFIG_FILE, O_READ);

	if (!configFile)
	{
		writeConfig();
		DEBUG(DEBUG_VERBOSE, Serial.println("config file missing"));
		return false;
	}

	String configText=configFile.readString();

	configFile.close();

	DynamicJsonBuffer jsonBuffer;

	JsonObject &root = jsonBuffer.parseObject(configText);

	config.iteration = root["iteration"];
	config.version = root["version"];
	if(root.containsKey("samplePeriod"))
		config.samplePeriodMins = ((unsigned long)root["samplePeriod"])/60;
	else
		config.samplePeriodMins = root["samplePeriodMin"];

#ifndef _SLEEP_PERCHANCE_TO_DREAM
	config.samplePeriodMins = 3;
#endif

	config.iterationSent=root["lastIterSent"];

	wifiInstance.ReadDetailsFromJSON(root, config.wifi);

	config.postHost.fromString((const char*)root["host"]);
	config.postHostPort = root["hostPort"];


	DEBUG(DEBUG_VERBOSE, Serial.printf("DynamicJsonBuffer readconfig size %d\n\r", jsonBuffer.size()));

	DEBUG(DEBUG_VERBOSE, Serial.println(configText));


	return true;
}

bool writeConfig()
{

	DEBUG(DEBUG_VERBOSE, Serial.printf("writing config %d \n\r", ESP.getFreeHeap()));

	DynamicJsonBuffer jsonBuffer;

	JsonObject &root = jsonBuffer.createObject();
	root["version"] = config.version;
	root["iteration"] = config.iteration;
	root["samplePeriodMin"]=config.samplePeriodMins;
	root["lastIterSent"] = config.iterationSent;

	wifiInstance.WriteDetailsToJSON(root, config.wifi);

	root["host"] = config.postHost.toString();
	root["hostPort"] = config.postHostPort;

	String jsonText;
	root.printTo(jsonText);

	File configFile = SD.open(_JSON_CONFIG_FILE, O_WRITE | O_CREAT | O_TRUNC);


	if (!configFile)
	{
		DEBUG(DEBUG_ERROR, Serial.println("could not create config"));
		return false;
	}
	else
	{
		DEBUG(DEBUG_VERBOSE, Serial.println("config opened"));
	}

	configFile.println(jsonText.c_str());

	configFile.close();

	DEBUG(DEBUG_VERBOSE, Serial.printf("DynamicJsonBuffer writeconfig size %d\n\r", jsonBuffer.size()));
	DEBUG(DEBUG_VERBOSE, Serial.println(jsonText));
	DEBUG(DEBUG_VERBOSE, Serial.println("written"));

	return true;


}


bool appendData(JsonObject &data)
{
	DEBUG(DEBUG_VERBOSE, Serial.println("appending data"));

	File dataFile = SD.open(_JSON_DATA_FILE, FILE_WRITE | O_APPEND);
	if (!dataFile)
	{
		dataFile.close();
		DEBUG(DEBUG_ERROR, Serial.println("could not create data"));
		return false;
	}
	if (dataFile.size() < _MAX_JSON_DATA_SIZE)
	{
		String dataText;
		data.printTo(dataText);
		DEBUG(DEBUG_VERBOSE, Serial.println(dataText));

		dataFile.println(dataText.c_str());

		dataFile.close();
	}
	else
	{
		DEBUG(DEBUG_VERBOSE, Serial.println("skipping addition, hostory too big"));
	}

	return true;
}

void GoToSleep(unsigned millisseconds)
{
	Serial.printf("Going into deep sleep for %d seconds\n\r", millisseconds/1000);

	// Connect D0 to RST to wake up
	pinMode(D0, WAKEUP_PULLUP);

	// sleep is in nano seconds
	ESP.deepSleep(millisseconds * 1000);

}

void PrepareDataBlob(JsonObject &root)
{
	root["host"] = wifiInstance.m_hostName.c_str();
	root["intervalS"] = config.samplePeriodMins*60;
	root["current"] = config.iteration;
	root["version"] = _MYVERSION;

}



DynamicJsonBuffer jsonHTTPsend;


#define _PY_PORT	5000

// the loop function runs over and over again until power down or reset
void loop() 
{
	// if we haven't completed config, handle the server exclusively
	if (wifiInstance.currentMode!=myWifiClass::wifiMode::modeSTA)
	{
		// depends, if we're AP we're fresh out of box, so we should give them time
		// if we're STAAP then we were just being polite back there, so kill early

		unsigned long sleepTimeOut = _AP_SLEEP_TIMEOUT, sleepTime= _AP_SLEEP_TIMEOUT_FOREVER;
		String reason("last activity");

		switch (wifiInstance.currentMode)
		{
		case myWifiClass::wifiMode::modeAP:
			sleepTimeOut = _AP_SLEEP_TIMEOUT_AP;
			sleepTime = _AP_SLEEP_TIMEOUT_FOREVER;
			reason="AP Mode";
			break;
		case myWifiClass::wifiMode::modeSTAandAP:
			sleepTimeOut = _AP_SLEEP_TIMEOUT_STAAP;
			sleepTime = _AP_SLEEP_AFTER_S(20);
			reason="AP_STA Mode";
			break;
		}

		if (millis() - lastSeenTraffic > sleepTimeOut)
		{
			DEBUG(DEBUG_IMPORTANT, Serial.printf("%s deadline exceeded - battery saving - sleeping forerver, sleeping %lu ms\n\r",reason.c_str(),sleepTime));
#ifdef _SLEEP_PERCHANCE_TO_DREAM
			GoToSleep(sleepTime);
#else
			delay(sleepTime);
#endif
		}
		wifiInstance.server.handleClient();
		return;
	}

	unsigned long millisAtStart = millis();

	DEBUG(DEBUG_INFO, Serial.printf("IP address: %s GW %s\n\r", WiFi.localIP().toString().c_str(),WiFi.gatewayIP().toString().c_str()));


	// end server
	// IPAddress pyHost(192, 168, 43, 22);


	float pressure=1, temp=2, humidity=3;
	bme.read(pressure, temp, humidity);

	// first, append this to the back of the dump
	DynamicJsonBuffer jsonBuffer;
	JsonObject &dataNow = jsonBuffer.createObject();

	dataNow["iter"] = ++config.iteration;
	dataNow["distCM"] = readDistanceCMS();
	dataNow["tempC"] = temp;
	dataNow["humid%"] = humidity;
	dataNow["pressMB"] = pressure;

	DEBUG(DEBUG_VERBOSE, Serial.println("new data"));
	DEBUG(DEBUG_VERBOSE, Serial.printf("DynamicJsonBuffer appenddata size %d\n\r", jsonBuffer.size()));

	String returnPayload;

	// if we're not connected, or if the datafile ALREADY exists, append this blob onto the back
	if ((WiFi.status() != WL_CONNECTED) || SD.exists(_JSON_DATA_FILE))
	{
		DEBUG(DEBUG_VERBOSE, Serial.println(SD.exists(_JSON_DATA_FILE)?"existing stale data":"wifi problem"));
		// append
		appendData(dataNow);

		// then deal with the data file, if we are connected
		if ((WiFi.status() == WL_CONNECTED))
		{
			SendCachedData(config.postHost, config.postHostPort, returnPayload);
		}

	}
	else
	{
		// try to send this blob
		jsonHTTPsend.clear();
		JsonObject &root = jsonHTTPsend.createObject();

		PrepareDataBlob(root);

		JsonArray &dataArray = root.createNestedArray("data");

		unsigned long a;
		float b, c, d, e;

		JsonObject &data = dataArray.createNestedObject();

		data["iter"] = a = dataNow["iter"];
		data["distCM"] = b = dataNow["distCM"];
		data["tempC"] = c = dataNow["tempC"];
		data["humid%"] = d = dataNow["humid%"];
		data["pressMB"] = e = dataNow["pressMB"];


		int httpCode = SendToHost(config.postHost, config.postHostPort, root, returnPayload,0);

		switch (httpCode)
		{
		case HTTP_CODE_OK:
		case HTTP_CODE_PROCESSING:
			config.iterationSent = config.iteration;;
			break;
		default:
			DEBUG(DEBUG_VERBOSE, Serial.println("retry later"));
			appendData(dataNow);
			break;
		}


	}


	writeConfig();

	signed long additionalSleepOffset = 0;

	// have a look at the payload
	DynamicJsonBuffer quickBuffer;
	JsonObject &serverInfo=quickBuffer.parse(returnPayload);
	if (serverInfo.success())
	{
		if (serverInfo.containsKey("reset"))
		{
			DEBUG(DEBUG_INFO, Serial.println("response includes reset key"));
			if (serverInfo["reset"] == true)
			{
				DEBUG(DEBUG_IMPORTANT, Serial.println("reset is true"));
				SD.remove(_JSON_CONFIG_FILE);
				SD.remove(_JSON_DATA_FILE);
				// reboot immediately if we're not going to uograde
				if (!serverInfo.containsKey("upgrade"))
				{
					ESP.restart();
				}
			}
		}
		
		if (serverInfo.containsKey("upgrade"))
		{
			JsonObject& updateDetails = serverInfo["upgrade"];
			// just blindly go to the url it gave me ... yeah - that feels drama free
			String host = updateDetails.containsKey("host")?updateDetails["host"]:config.postHost.toString();
			int port = updateDetails.containsKey("port")?updateDetails["port"]:config.postHostPort;
			String url = updateDetails["url"];
			DEBUG(DEBUG_IMPORTANT, Serial.printf("Updating from %s:%d %s\n\r", (const char*)host.c_str(),port, (const char*)url.c_str()));
			// do it!
			enum HTTPUpdateResult result=ESPhttpUpdate.update(host,port,url, _MYVERSION);

			switch (result)
			{
			case HTTP_UPDATE_FAILED:
				DEBUG(DEBUG_ERROR, Serial.println("updated FAILED"));
				break;
			case HTTP_UPDATE_NO_UPDATES:
				DEBUG(DEBUG_IMPORTANT, Serial.println("no updates"));
				break;
			case HTTP_UPDATE_OK:
				DEBUG(DEBUG_IMPORTANT, Serial.println("update succeeded"));
				break;
			}

		}

		// try to align with the time
		if (serverInfo.containsKey("minutes"))
		{
			int currentMinutes = serverInfo["minutes"];
			unsigned minsToSkip = (currentMinutes%config.samplePeriodMins);

			additionalSleepOffset = (minsToSkip *60)*1000;
		}
		if (serverInfo.containsKey("seconds"))
		{
			additionalSleepOffset += ((int)serverInfo["seconds"]) * 1000;
		}



	}
	else
	{
		DEBUG(DEBUG_VERBOSE, Serial.println("failed to parse response"));
	}


	unsigned long millisAtEnd = millis();

	unsigned long millisToSleep = (config.samplePeriodMins *60 * 1000) - (millisAtEnd - millisAtStart);

	if ((unsigned)additionalSleepOffset > (millisToSleep / 2))
	{
		millisToSleep += (millisToSleep-additionalSleepOffset);
		DEBUG(DEBUG_VERBOSE, Serial.printf("Sleeping for %lu + %lu ms\n\r", millisToSleep, (millisToSleep - additionalSleepOffset)));

	}
	else
	{
		millisToSleep -= additionalSleepOffset;
		DEBUG(DEBUG_VERBOSE,Serial.printf("Sleeping for %lu - %lu ms\n\r", millisToSleep, additionalSleepOffset));
	}




#ifdef _SLEEP_PERCHANCE_TO_DREAM
	DEBUG(DEBUG_INFO, Serial.printf("Awake for %lu ms\n\r",millis()-millisAtBoot));
	GoToSleep(millisToSleep);
#else
	delay(millisToSleep);
#endif

}

// from a cold boot the sensor needs ~350ms to be ready to take a reading
// use the A-D voltage
float readDistanceCMS()
{
	// long way round
	//float ana = analogRead(A0);
	//float anaV = 3.3 * (ana / 1024);
	//float scale = 3.3 / 512.0;
	//float theRange = (anaV/scale)*2.54;
	return (analogRead(A0) / 2)*2.54;
}


int SendToHost(IPAddress &host, unsigned port, JsonObject &blob, String &returnPayload, int retries)
{
	if (retries < 1)
		retries = 1;

	String *body = new String();
	size_t width = blob.printTo(*body);

	DEBUG(DEBUG_VERBOSE, Serial.println("===================="));
	DEBUG(DEBUG_VERBOSE, Serial.printf("Body - JSON length = %d\n\r", width));
	DEBUG(DEBUG_VERBOSE, Serial.println(*body));
	DEBUG(DEBUG_VERBOSE, Serial.println("===================="));
	int httpCode = HTTPC_ERROR_CONNECTION_REFUSED;
	HTTPClient http;
	
	// milliseconds!
	http.setTimeout(10*1000);


	if (http.begin(host.toString().c_str(), port, "/data"))
	{

		http.addHeader("Content-Type", "application/json");

		for (int retryAttempt = 0; (retryAttempt < retries) && httpCode == HTTPC_ERROR_CONNECTION_REFUSED; retryAttempt++)
		{
			DEBUG(DEBUG_VERBOSE, Serial.println("Posting"));
			httpCode = http.POST(*body);

			if (httpCode == HTTPC_ERROR_CONNECTION_REFUSED)
			{
#ifdef _TRY_PING
				if (Ping.ping(host,1))
				{
					DEBUG(DEBUG_IMPORTANT, Serial.println("PING success"));
				}
				else
				{
					DEBUG(DEBUG_IMPORTANT, Serial.println("PING FAILED"));
				}
#endif
				DEBUG(DEBUG_VERBOSE, Serial.println("Connection refused"));

			}
			else
			{
				// anything to sump from the server
				int payloadSize=http.getSize();
				if (payloadSize)
				{
					returnPayload = http.getString();
					DEBUG(DEBUG_INFO, Serial.printf("response payloadsize %d\n\r", payloadSize));
					DEBUG(DEBUG_INFO, Serial.println(returnPayload));
				}
			}
		}

		DEBUG(DEBUG_VERBOSE, Serial.printf("Post result %d\n\r", httpCode));

		http.end();
	}
	else
	{
		DEBUG(DEBUG_IMPORTANT, Serial.println("http.begin failed"));
		httpCode = HTTPC_ERROR_CONNECTION_REFUSED;
	}
	delete body;

	return httpCode;

}

bool SendCachedData(IPAddress &pyHost, unsigned port, String &returnPayload)
{
	if ((WiFi.status() == WL_CONNECTED))
	{

		File dataFile = SD.open(_JSON_DATA_FILE, FILE_READ);


		if (dataFile)
		{
			unsigned long lastIterSeen = 0;
			bool flagDataFileForDelete = false;


			// Google Sheets API has a limit of 500 requests per 100 seconds per project, and 100 requests per 100 seconds per user
			bool abortMore = false;
			// server can't handle concurrent requests 
			for (int loopCount = 0;!abortMore && (loopCount < 1) && dataFile.available(); )
			{
				// this takes a while - this isn't NetWare, OS calls don't yied, so do it explicitly
				yield();

				jsonHTTPsend.clear();
				JsonObject &root = jsonHTTPsend.createObject();

				PrepareDataBlob(root);

				DEBUG(DEBUG_VERBOSE, Serial.println("aggregating stale data"));

				JsonArray &dataArray = root.createNestedArray("data");

#define _MAX_ROW_COUNT	5

				for (int rowCount = 0; (rowCount < _MAX_ROW_COUNT) && dataFile.available(); )
				{

					String jsonText = dataFile.readStringUntil(_JSON_DATA_SEPARATOR);

					DynamicJsonBuffer temp;
					JsonObject &readData = temp.parse(jsonText);
					DEBUG(DEBUG_VERBOSE, Serial.printf("DynamicJsonBuffer temp size %d\n\r", temp.size()));

					if (readData.success())
					{

						lastIterSeen = readData["iter"];

						if (lastIterSeen > config.iterationSent)
						{
							DEBUG(DEBUG_VERBOSE, Serial.printf("%d. (%d) %s\n\r", rowCount+1, jsonText.length(), jsonText.c_str()));

							JsonObject &data = dataArray.createNestedObject();

							// cloning between objects is 'hard' so go via intermediate var

							unsigned long a;
							float b, c, d, e;

							data["iter"] = a = readData["iter"];
							data["distCM"] = b = readData["distCM"];
							data["tempC"] = c = readData["tempC"];
							data["humid%"] = d = readData["humid%"];
							data["pressMB"] = e = readData["pressMB"];

							rowCount++;
						}
					}
					else
					{
						DEBUG(DEBUG_IMPORTANT, Serial.println("failed to parse stale data row"));
					}

					DEBUG(DEBUG_VERBOSE, Serial.printf("DynamicJsonBuffer jsonHTTPsend size %u\n\r", jsonHTTPsend.size()));

				}

				yield();
				
				int httpCode = SendToHost(pyHost, port, root, returnPayload,0);

				switch (httpCode)
				{
				case HTTP_CODE_OK:
				case HTTP_CODE_PROCESSING:
					config.iterationSent = lastIterSeen;
					if (!dataFile.available() && config.iterationSent == config.iteration)
					{
						flagDataFileForDelete = true;
					}
					break;
				default:
					DEBUG(DEBUG_VERBOSE, Serial.println("failed, bailing"));
					abortMore = true;
					break;
				}

			}

			dataFile.close();
			// if there's nothing left in the file, kill it
			if (flagDataFileForDelete)
			{
				SD.remove(_JSON_DATA_FILE);
				DEBUG(DEBUG_INFO, Serial.println("Killing data file"));
			}



		}
		else
		{
			DEBUG(DEBUG_IMPORTANT, Serial.println("Could not open data"));
			return false;
		}
	}
	else
	{
		DEBUG(DEBUG_ERROR, Serial.println("not connected"));
		return false;
	}

	return true;

}
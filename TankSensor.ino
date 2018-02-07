/*
 Name:		TankSensor.ino
 Created:	1/3/2018 5:14:48 PM
 Author:	bflint
*/

#include <SPI.h>
#include <ESP8266HTTPClient.h>
#include <hardwareSerial.h>

#include <WiFiClient.h>

#include <ArduinoJson.h>

#define _JSON_CONFIG_FILE "CONFIG.JSN"

#define _JSON_DATA_FILE			"DATA.JSN"
#define _JSON_DATA_SEPARATOR	'\n'

#define _JSON_BODY_LEN 500

#define _SAMPLE_INTERVAL_S	(60*5)


#include <SD.h>

//#define _TRY_PING
#ifdef _TRY_PING
#include <ESP8266Ping.h>
#endif

#include <Wire.h>


// my libs
#include <myWifi.h>
myWifiClass wifiInstance;



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



//#define _SLEEP_PERCHANCE_TO_DREAM

#ifdef _SLEEP_PERCHANCE_TO_DREAM
// how long to sleep
#define _SLEEP_SECONDS	_SAMPLE_INTERVAL_S

#endif

myWifiClass::wifiDetails thisDetails = {
	"beige","0404407219",true,false,IPAddress(192,168,42,105),IPAddress(255,255,255,0),IPAddress(192,168,42,250)
};

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

	delay(1000);


	//DEBUG(DEBUG_VERBOSE,Serial.println("I'm awake. Snoozing for 10s"));
	//delay(10000);
	DEBUG(DEBUG_VERBOSE, Serial.println("I'm REALLY awake."));
	digitalWrite(TRAN_PIN, HIGH);

	wifiInstance.ConnectWifi(myWifiClass::modeSTA, thisDetails);

	// see comment for TRAN_PIN
	if (!SD.begin(D4))
	{
		DEBUG(DEBUG_ERROR,Serial.println("SD card failed"));
	}

#ifdef  _CLEAR_DATA

	SD.remove(_JSON_CONFIG_FILE);
	SD.remove(_JSON_DATA_FILE);

#endif //  _CLEAR_DATA



	readConfig();


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


#ifdef _SLEEP_PERCHANCE_TO_DREAM

	GoToSleep(_SLEEP_SECONDS);

#endif

}



struct {
	unsigned version;
	unsigned long iteration, iterationSent;
	unsigned long samplePeriod;
} config=
{
	0,					// ver
	0,0,				// iters
	_SAMPLE_INTERVAL_S
};

bool readConfig()
{


	DEBUG(DEBUG_VERBOSE, Serial.println("reading config"));

	// try to read the config - if it fails, create the default
	File configFile=SD.open(_JSON_CONFIG_FILE, O_READ);

	if (!configFile)
	{
		//configFile.close();
		DEBUG(DEBUG_VERBOSE, Serial.println("config file missing"));
		return false;
	}

	String configText=configFile.readString();

	configFile.close();

	DynamicJsonBuffer jsonBuffer;

	JsonObject &root = jsonBuffer.parseObject(configText);

	config.iteration = root["iteration"];
	config.version = root["version"];
	config.samplePeriod = root["samplePeriod"];
	config.iterationSent=root["lastIterSent"];

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
	root["samplePeriod"]=config.samplePeriod;
	root["lastIterSent"] = config.iterationSent;

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

	String dataText;
	data.printTo(dataText);
	DEBUG(DEBUG_VERBOSE, Serial.println(dataText));

	dataFile.println(dataText.c_str());
	DEBUG(DEBUG_VERBOSE, Serial.println("written"));


	dataFile.close();

	return true;
}

void GoToSleep(unsigned seconds)
{
	Serial.printf("Going into deep sleep for %d seconds", seconds);

	// Connect D0 to RST to wake up
	pinMode(D0, WAKEUP_PULLUP);

	ESP.deepSleep(seconds * 1000000);

}




DynamicJsonBuffer jsonHTTPsend;


#define _PY_PORT	5000

// the loop function runs over and over again until power down or reset
void loop() 
{

	unsigned long millisAtStart = millis();

	DEBUG(DEBUG_INFO, Serial.printf("IP address: %s GW %s\n\r", WiFi.localIP().toString().c_str(),WiFi.gatewayIP().toString().c_str()));


	// end server
	IPAddress pyHost(192, 168, 43, 22);


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

	DEBUG(DEBUG_VERBOSE, Serial.println("appending data"));
	DEBUG(DEBUG_VERBOSE, Serial.printf("DynamicJsonBuffer appenddata size %d\n\r", jsonBuffer.size()));

	// if we're not connected, or if the datafile ALREADY exists, append this blob onto the back
	if ((WiFi.status() != WL_CONNECTED) || SD.exists(_JSON_DATA_FILE))
	{
		// append
		appendData(dataNow);

		// then deal with the data file, if we are connected
		if ((WiFi.status() == WL_CONNECTED))
		{
			SendCachedData(pyHost, _PY_PORT);
		}

	}
	else
	{
		// try to send this blob
		jsonHTTPsend.clear();
		JsonObject &root = jsonHTTPsend.createObject();

		root["host"] = wifiInstance.m_hostName.c_str();
		root["intervalS"] = config.samplePeriod;
		root["current"] = config.iteration;

		JsonArray &dataArray = root.createNestedArray("data");

		unsigned long a;
		float b, c, d, e;

		JsonObject &data = dataArray.createNestedObject();

		data["iter"] = a = dataNow["iter"];
		data["distCM"] = b = dataNow["distCM"];
		data["tempC"] = c = dataNow["tempC"];
		data["humid%"] = d = dataNow["humid%"];
		data["pressMB"] = e = dataNow["pressMB"];

		int httpCode = SendToHost(pyHost, _PY_PORT, root);

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

	unsigned long millisAtEnd = millis();

	unsigned long millisToSleep = (config.samplePeriod * 1000) - (millisAtEnd - millisAtStart);

	DEBUG(DEBUG_VERBOSE,Serial.printf("Sleeping for %lu ms\n\r", millisToSleep));

	delay(millisToSleep);

}

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


int SendToHost(IPAddress &host, unsigned port, JsonObject &blob)
{

	String *body = new String();
	size_t width = blob.printTo(*body);

	DEBUG(DEBUG_VERBOSE, Serial.printf("JSON length = %d\n\r", width));
	DEBUG(DEBUG_VERBOSE, Serial.println(*body));
	int httpCode = HTTPC_ERROR_CONNECTION_REFUSED;
	HTTPClient http;
	
	// milliseconds!
	http.setTimeout(10*1000);


	if (http.begin(host.toString().c_str(), port, "/data"))
	{

		http.addHeader("Content-Type", "application/json");

		for (int retryAttempt = 0; (retryAttempt < 3) && httpCode == HTTPC_ERROR_CONNECTION_REFUSED; retryAttempt++)
		{
			DEBUG(DEBUG_VERBOSE, Serial.println("Posting"));
			httpCode = http.POST(*body);
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

bool SendCachedData(IPAddress &pyHost, unsigned port)
{
	if ((WiFi.status() == WL_CONNECTED))
	{


#ifdef _TRY_PING
		if (Ping.ping(pyHost))
		{
			DEBUG(DEBUG_IMPORTANT, Serial.println("PING success"));
		}
		else
		{
			DEBUG(DEBUG_IMPORTANT, Serial.println("PING FAILED"));
		}
#endif

		File dataFile = SD.open(_JSON_DATA_FILE, FILE_READ);


		if (dataFile)
		{
			unsigned long lastIterSeen = 0;
			bool flagDataFileForDelete = false;

			{
				jsonHTTPsend.clear();
				JsonObject &root = jsonHTTPsend.createObject();

				root["host"] = wifiInstance.m_hostName.c_str();
				root["intervalS"] = config.samplePeriod;
				root["current"] = config.iteration;

				JsonArray &dataArray = root.createNestedArray("data");

				// Google Sheets API has a limit of 500 requests per 100 seconds per project, and 100 requests per 100 seconds per user



				for (int loopCount = 0; (loopCount < 10) && dataFile.available(); )
				{
					// this takes a while - this isn't NetWare, OS calls don't yied, so do it explicitly
					yield();

					String jsonText = dataFile.readStringUntil(_JSON_DATA_SEPARATOR);


					DynamicJsonBuffer temp;
					JsonObject &readData = temp.parse(jsonText);
					DEBUG(DEBUG_VERBOSE, Serial.printf("DynamicJsonBuffer temp size %d\n\r", temp.size()));

					if (readData.success())
					{

						lastIterSeen = readData["iter"];

						if (lastIterSeen > config.iterationSent)
						{
							DEBUG(DEBUG_VERBOSE, Serial.printf("%d. (%d) %s\n\r", loopCount + 1, jsonText.length(), jsonText.c_str()));

							JsonObject &data = dataArray.createNestedObject();

							// cloning between objects is 'hard' so go via intermediate var

							unsigned long a;
							float b, c, d, e;

							data["iter"] = a = readData["iter"];
							data["distCM"] = b = readData["distCM"];
							data["tempC"] = c = readData["tempC"];
							data["humid%"] = d = readData["humid%"];
							data["pressMB"] = e = readData["pressMB"];

							loopCount++;
						}
					}
					else
					{
						DEBUG(DEBUG_IMPORTANT, Serial.println("failed to parse"));
					}

				}

				DEBUG(DEBUG_VERBOSE, Serial.printf("DynamicJsonBuffer jsonHTTPsend size %u\n\r", jsonHTTPsend.size()));

				yield();


				{
					int httpCode = SendToHost(pyHost, port, root);

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
						DEBUG(DEBUG_VERBOSE, Serial.println("retry later"));
						break;
					}

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
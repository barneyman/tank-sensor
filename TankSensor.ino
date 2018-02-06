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
//#include <FS.h>

#define _JSON_CONFIG_FILE "CONFIG.JSN"

#define _JSON_DATA_FILE			"DATA.JSN"
#define _JSON_DATA_SEPARATOR	'\n'


#define _SAMPLE_INTERVAL_S	10
#define _SAMPLES_PER_BULK	2

//#define _NO_HTTP

#include <SD.h>

//#define _TRY_PING
#ifdef _TRY_PING
#include <ESP8266Ping.h>
#endif

#include <Wire.h>

//#define USE_BMP

// my libs
#include <myWifi.h>
myWifiClass wifiInstance;


#ifdef USE_BMP
#include <BMP280.h>

BMP280 bmp;
#else

#include <BME280I2C.h>

BME280I2C::Settings mySettings = {

	/*OSR _tosr =			*/BME280::OSR_X1,
	/*OSR _hosr =			*/BME280::OSR_X1,
	/*OSR _posr =			*/BME280::OSR_X1,
	/*Mode _mode =			*/BME280::Mode_Forced,
	/*StandbyTime _st =		*/BME280::StandbyTime_1000ms,
	/*Filter _filter =		*/BME280::Filter_Off,
	/*SpiEnable _se =		*/BME280::SpiEnable_False,
	/*I2CAddr _addr =		*/BME280I2C::I2CAddr_0x76


};



BME280I2C bme(mySettings);
#endif



//#define _SLEEP_PERCHANCE_TO_DREAM

#ifdef _SLEEP_PERCHANCE_TO_DREAM
// how long to sleep
#define _SLEEP_SECONDS	25

#endif

myWifiClass::wifiDetails thisDetails = {
	"beige","0404407219",true,true,IPAddress(192,168,42,105),IPAddress(255,255,255,0),IPAddress(192,168,42,250)
};

#define _CLEAR_DATA

// the setup function runs once when you press reset or power the board
void setup() {

	Serial.begin(115200);
	///Serial.setTimeout(2000);

	delay(1000);

	Serial.println("I'm awake. Sleeping for 10s");
	delay(10000);

	wifiInstance.ConnectWifi(myWifiClass::modeSTA, thisDetails);

	if (!SD.begin(D8))
	{
		DEBUG(DEBUG_ERROR,Serial.println("SD card failed"));
	}
	else
	{
		DEBUG(DEBUG_VERBOSE, Serial.printf("SDFile is %d bytes\n\r", sizeof(SdFile)));
	}

#ifdef  _CLEAR_DATA

	SD.remove(_JSON_CONFIG_FILE);
	SD.remove(_JSON_DATA_FILE);

#endif //  _CLEAR_DATA



	readConfig();

#ifdef USE_BMP

	if (!bmp.begin(D2, D1))
	{
		Serial.printf("bmp failed %d \r\n",bmp.getError());
	}
#else

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

#endif



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




// the loop function runs over and over again until power down or reset
void loop() 
{

	unsigned long millisAtStart = millis();

	DEBUG(DEBUG_INFO, Serial.printf("IP address: %s GW %s\n\r", WiFi.localIP().toString().c_str(),WiFi.gatewayIP().toString().c_str()));



#ifdef USE_BMP
	double pressure = 1, temp = 2, humidity = 3;
	bmp.getTemperatureAndPressure(temp, pressure);
#else
	float pressure=1, temp=2, humidity=3;
	bme.read(pressure, temp, humidity);
#endif

	// false scope to kill the json buffer
	{
		// first, append this to the back of the dump
		DynamicJsonBuffer jsonBuffer;
		JsonObject &data = jsonBuffer.createObject();

		data["iter"] = ++config.iteration;
		data["distCM"] = readDistanceCMS();
		data["tempC"] = temp;
		data["humid%"] = humidity;
		data["pressMB"] = pressure;

		DEBUG(DEBUG_VERBOSE, Serial.println("appending data"));
		DEBUG(DEBUG_VERBOSE, Serial.printf("DynamicJsonBuffer appenddata size %d\n\r", jsonBuffer.size()));

		appendData(data);
	}


	if ((WiFi.status() == WL_CONNECTED))
	{

#if defined (_TRY_PING) || !defined (_NO_HTTP)
		// end server
		IPAddress pyHost(192, 168, 43, 18);

#endif

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
			jsonHTTPsend.clear();
			JsonObject &root = jsonHTTPsend.createObject();

			root["host"] = wifiInstance.m_hostName.c_str();
			root["intervalS"] = config.samplePeriod;
			root["current"] = config.iteration;

			JsonArray &dataArray = root.createNestedArray("data");

			unsigned long lastIterSeen = 0;
			for (int loopCount = 0; (loopCount < 5) && dataFile.available(); loopCount++)
			{
				// this takes a while - this isn't NetWare, OS calls don't yied, so do it explicitly
				yield();

				String jsonText = dataFile.readStringUntil(_JSON_DATA_SEPARATOR);

				DEBUG(DEBUG_VERBOSE, Serial.printf("%d. (%d) %s\n\r", loopCount+1, jsonText.length(), jsonText.c_str()));

				DynamicJsonBuffer temp;
				JsonObject &readData = temp.parse(jsonText);
				DEBUG(DEBUG_VERBOSE, Serial.printf("DynamicJsonBuffer temp size %d\n\r", temp.size()));

				if (readData.success())
				{

					lastIterSeen = readData["iter"];

					if (lastIterSeen > config.iterationSent)
					{
						JsonObject &data = dataArray.createNestedObject();

						// cloning between objetcs is 'hard' so go via intermediate var

						unsigned long a;
						float b, c, d, e;

						data["iter"] =a= readData["iter"];
						data["distCM"] =b= readData["distCM"];
						data["tempC"] =c= readData["tempC"];
						data["humid%"] =d= readData["humid%"];
						data["pressMB"] =e= readData["pressMB"];

					}
				}
				else
				{
					DEBUG(DEBUG_IMPORTANT, Serial.println("failed to parse"));
				}

			}

			DEBUG(DEBUG_VERBOSE, Serial.printf("DynamicJsonBuffer jsonHTTPsend size %u\n\r", jsonHTTPsend.size()));

			yield();

#define _JSON_BODY_LEN 500

			bool flagDataFileForDelete = false;
			{
				// done here as an array because using a String causes a WDT reset 
				//char *body=new char[_JSON_BODY_LEN];
				//size_t width=root.printTo(body, _JSON_BODY_LEN);

				String *body = new String();
				size_t width = root.printTo(*body);

#ifndef _NO_HTTP
				DEBUG(DEBUG_VERBOSE,Serial.printf("JSON length = %d\n\r", width));
				DEBUG(DEBUG_VERBOSE,Serial.println(*body));
				int httpCode = HTTPC_ERROR_CONNECTION_REFUSED;
				HTTPClient http;
				http.setTimeout(30);

				if (http.begin(pyHost.toString().c_str(), 5000, "/data"))
				{
					DEBUG(DEBUG_VERBOSE, Serial.println("Posting"));

					http.addHeader("Content-Type", "application/json");
					httpCode = http.POST(*body);

					DEBUG(DEBUG_VERBOSE, Serial.printf("Post result %d\n\r", httpCode));

					http.end();
				}
				else
				{
					DEBUG(DEBUG_IMPORTANT, Serial.println("http.begin failed"));
				}

				switch (httpCode)
				{
				case HTTP_CODE_OK:
				case HTTP_CODE_PROCESSING:
					config.iterationSent = lastIterSeen;
					if (!dataFile.available() && config.iterationSent==config.iteration)
					{
						flagDataFileForDelete = true;
					}
					break;
				default:
					DEBUG(DEBUG_VERBOSE, Serial.println("retry later"));
					break;
				}
#endif
				delete body;
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
		}


	}
	else
	{
		DEBUG(DEBUG_ERROR, Serial.println("could not create data"));
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

#ifdef USE_BMP

void readTandP()
{
	double T, P;
	char result = bmp.startMeasurment();

	if (result != 0) {
		delay(result);
		result = bmp.getTemperatureAndPressure(T, P);

		if (result != 0)
		{
			//double A = bmp.altitude(P, P0);

			Serial.print("T = \t"); Serial.print(T, 2); Serial.print(" degC\t");
			Serial.print("P = \t"); Serial.print(P, 2); Serial.print(" mBar\t");
			//Serial.print("A = \t"); Serial.print(A, 2); Serial.println(" m");

		}
		else {
			Serial.printf("Error2. %d\n\r",bmp.getError());
		}
	}
	else {
		Serial.printf("Error1. %d\n\r", bmp.getError());
	}

	Serial.println();
}

#endif


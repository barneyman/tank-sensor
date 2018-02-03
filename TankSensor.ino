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
#define JSON_STATIC_BUFSIZE	2048
StaticJsonBuffer<JSON_STATIC_BUFSIZE> jsonBuffer;

#define _JSON_DATA_FILE			"DATA.JSN"
#define _JSON_DATA_SEPARATOR	'|'


#define _SAMPLE_INTERVAL_S	10
#define _SAMPLES_PER_BULK	2


//#define _USE_SD
#define _USE_SD_MIN

#if defined(_USE_SD) || defined(_USE_SD_MIN)
#include <SD.h>
#endif

#define _NO_HTTP

//#define _TRY_PING
#ifdef _TRY_PING
#include <ESP8266Ping.h>
#endif

#include <Wire.h>

//#define USE_BMP

// my libs
#include <bjfWifi.h>
bjfWifi wifiInstance;


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

bjfWifi::wifiDetails thisDetails = {
	"beige","0404407219",true,false,IPAddress(192,168,42,105),IPAddress(255,255,255,0),IPAddress(192,168,42,250)
};

#define _CLEAR_DATA

// the setup function runs once when you press reset or power the board
void setup() {

	Serial.begin(9600);
	///Serial.setTimeout(2000);

	delay(1000);

	pinMode(D3, OUTPUT);

	Serial.println("I'm awake.");

	wifiInstance.ConnectWifi(bjfWifi::modeSTA, thisDetails);

#if defined( _USE_SD_MIN ) || defined (_USE_SD)
	if (!SD.begin(D8))
	{
		DEBUG(DEBUG_ERROR,Serial.println("SD card failed"));
	}
	else
	{
		DEBUG(DEBUG_VERBOSE, Serial.printf("SDFile is %d bytes\n\r", sizeof(SdFile)));
	}
#endif

#ifdef  _CLEAR_DATA

#if defined(_USE_SD) || defined(_USE_SD_MIN)
	SD.remove(_JSON_CONFIG_FILE);
	SD.remove(_JSON_DATA_FILE);
#endif

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

#if  defined (_USE_SD_MIN)

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

	StaticJsonBuffer<JSON_STATIC_BUFSIZE> jsonBuffer;

	JsonObject &root = jsonBuffer.parseObject(configText);

	config.iteration = root["iteration"];
	config.version = root["version"];
	config.samplePeriod = root["samplePeriod"];

	DEBUG(DEBUG_VERBOSE, Serial.println(configText));
#endif


	return true;
}

bool writeConfig()
{
#if defined (_USE_SD_MIN)

	DEBUG(DEBUG_VERBOSE, Serial.printf("writing config %d \n\r", ESP.getFreeHeap()));

	StaticJsonBuffer<JSON_STATIC_BUFSIZE> jsonBuffer;

	JsonObject &root = jsonBuffer.createObject();
	root["version"] = config.version;
	root["iteration"] = config.iteration;
	root["samplePeriod"]=config.samplePeriod;

	String jsonText;
	root.printTo(jsonText);

	File configFile = SD.open(_JSON_CONFIG_FILE, O_WRITE | O_CREAT | O_TRUNC);
	//File configFile = SD.open(_JSON_CONFIG_FILE, FILE_WRITE | O_APPEND);


	if (!configFile)
	{
		configFile.close();
		DEBUG(DEBUG_ERROR, Serial.println("could not create config"));
		return false;
	}
	else
	{
		DEBUG(DEBUG_VERBOSE, Serial.println("config opened"));
	}

	configFile.println(jsonText.c_str());

	configFile.close();

	DEBUG(DEBUG_VERBOSE, Serial.println(jsonText));
	DEBUG(DEBUG_VERBOSE, Serial.println("written"));
#endif

	return true;


}


bool appendData(JsonObject &data)
{

#ifdef _USE_SD_MIN

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
	dataFile.write(_JSON_DATA_SEPARATOR);
	DEBUG(DEBUG_VERBOSE, Serial.println("written"));


	dataFile.close();

#endif

	return true;
}

void GoToSleep(unsigned seconds)
{
	Serial.printf("Going into deep sleep for %d seconds", seconds);

	// Connect D0 to RST to wake up
	pinMode(D0, WAKEUP_PULLUP);

	ESP.deepSleep(seconds * 1000000);

}






// the loop function runs over and over again until power down or reset
void loop() 
{
	unsigned long millisAtStart = millis();

	digitalWrite(D3, HIGH);

	DEBUG(DEBUG_INFO, Serial.printf("IP address: %s GW %s\n\r", WiFi.localIP().toString().c_str(),WiFi.gatewayIP().toString().c_str()));



#ifdef USE_BMP
	double pressure = 1, temp = 2, humidity = 3;
	bmp.getTemperatureAndPressure(temp, pressure);
#else
	float pressure=1, temp=2, humidity=3;
	bme.read(pressure, temp, humidity);
#endif

	DEBUG(DEBUG_VERBOSE, Serial.printf("%f %f %f\n\r",pressure,temp,humidity));

	// first, append this to the back of the dump
	jsonBuffer.clear();
	JsonObject &data = jsonBuffer.createObject();



	data["sent"] = 0;
	data["iter"] = ++config.iteration;
	data["distCM"] = readDistanceCMS();
	data["tempC"] = temp;
	data["humid%"] = humidity;
	data["pressMB"] = pressure;

//	appendData(data);

	DEBUG(DEBUG_VERBOSE, Serial.println("appending data"));

	File dataFile = SD.open(_JSON_DATA_FILE, FILE_WRITE | O_APPEND);
	if (dataFile)
	{

	String dataText;
	data.printTo(dataText);
	DEBUG(DEBUG_VERBOSE, Serial.println(dataText));

	dataFile.println(dataText.c_str());
	dataFile.write(_JSON_DATA_SEPARATOR);
	DEBUG(DEBUG_VERBOSE, Serial.println("written"));

	}
	else
	{
		dataFile.close();
		DEBUG(DEBUG_ERROR, Serial.println("could not create data"));
	}





	if(dataFile && (WiFi.status() == WL_CONNECTED))
	{
		dataFile.flush();
		dataFile.seek(SEEK_SET);

		// end server
		IPAddress pyHost(192,168,43,18);

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

#ifdef _USE_SD_MIN

		if (dataFile)
		{
			jsonBuffer.clear();
			JsonObject &root = jsonBuffer.createObject();
			JsonArray &dataArray = root.createNestedArray("data");

			root["host"] = wifiInstance.m_hostName.c_str();
			root["intervalS"] = config.samplePeriod;

			unsigned long lastIterSeen = 0;

			for (int loopCount = 0; loopCount < 5 && dataFile.available(); loopCount++)
			{

				String jsonText = dataFile.readStringUntil(_JSON_DATA_SEPARATOR);

				DEBUG(DEBUG_VERBOSE, Serial.printf("%d %s\n\r", loopCount, jsonText.c_str()));

				StaticJsonBuffer<1024> temp;
				JsonObject &readData = temp.parse(jsonText);

				lastIterSeen = readData["iter"];

				if (lastIterSeen > config.iterationSent)
				{
					JsonObject &data = dataArray.createNestedObject();
					data["iter"] = readData["iter"];
					data["distCM"] = readData["distCM"];
					data["tempC"] = readData["tempC"];
					data["humid%"] = readData["humid%"];
					data["pressMB"] = readData["pressMB"];

				}
			}

#ifndef _NO_HTTP

			String body;
			root.printTo(body);
			Serial.printf("JSON length = %d\n\r", body.length());
			Serial.println(body);
			int httpCode = HTTPC_ERROR_CONNECTION_REFUSED;
			HTTPClient http;
			http.setTimeout(30);
			if (http.begin(pyHost.toString().c_str(), 5000, "/data"))
			{

				http.addHeader("Content-Type", "application/json");
				httpCode = http.POST(body);

				Serial.printf("Post result %d\n\r", httpCode);

				http.end();
			}
			else
			{
				Serial.println("http failed");
			}

			switch (httpCode)
			{
			case HTTP_CODE_OK:
			case HTTP_CODE_PROCESSING:
				config.iterationSent = lastIterSeen;
				break;
			default:
				Serial.println("retry later");
				break;
			}

#endif


		}
		else
		{
			DEBUG(DEBUG_IMPORTANT, Serial.println("Could not open data"));
		}



#endif







	}	
	else
	{
		Serial.println("wifi failed");
	}

	dataFile.close();

	digitalWrite(D3, LOW);

	writeConfig();

	unsigned long millisAtEnd = millis();

	unsigned long millisToSleep = (config.samplePeriod * 1000) - (millisAtEnd - millisAtStart);

	Serial.printf("Sleeping for %lu ms\n\r", millisToSleep);

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


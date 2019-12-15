#include "config.h"
#include "debug.h"
#include "mqtt_client.h"
#include <IotWebConf.h>
#include "EEPROM.h"
#include <ESP8266WiFi.h>
#include "door_opener.h"

void wifiConnected();
void configSaved();

DNSServer dnsServer;
WebServer server(80);
HTTPUpdateServer httpUpdater;
WiFiClient net;

MqttConfig mqttConfig;
MqttClient mqttClient;

IotWebConf iotWebConf("DoorGuard", &dnsServer, &server, "", CONFIG_VERSION);

IotWebConfParameter params[] = {
	IotWebConfParameter("MQTT server", "mqttServer", mqttConfig.server, sizeof(mqttConfig.server), "text", NULL, mqttConfig.server, NULL, true),
	IotWebConfParameter("MQTT port", "mqttPort", mqttConfig.port, sizeof(mqttConfig.port), "text", NULL, mqttConfig.port, NULL, true),
	IotWebConfParameter("MQTT username", "mqttUsername", mqttConfig.username, sizeof(mqttConfig.username), "text", NULL, mqttConfig.username, NULL, true),
	IotWebConfParameter("MQTT password", "mqttPassword", mqttConfig.password, sizeof(mqttConfig.password), "password", NULL, mqttConfig.password, NULL, true),
	IotWebConfParameter("MQTT topic", "mqttTopic", mqttConfig.topic, sizeof(mqttConfig.topic), "text", NULL, mqttConfig.topic, NULL, true)};

boolean needReset = false;
boolean connected = false;

DoorOpener doorOpener;

void setup()
{
	// Setup debugging stuff
	SERIAL_DEBUG_SETUP(115200);

	// Setup WiFi and config stuff
	DEBUG("Setting up WiFi and config stuff.");
	DEBUG("Setting status pin to %d.", STATUS_PIN);
	iotWebConf.setStatusPin(STATUS_PIN);
	for (uint8_t i = 0; i < sizeof(params) / sizeof(params[0]); i++)
	{
		DEBUG("Added parameter %s.", params[i].label);
		iotWebConf.addParameter(&params[i]);
	}
	iotWebConf.setConfigSavedCallback(&configSaved);
	iotWebConf.setWifiConnectionCallback(&wifiConnected);
	iotWebConf.setupUpdateServer(&httpUpdater);

	boolean validConfig = iotWebConf.init();
	if (!validConfig)
	{
		DEBUG("Missing or invalid config. MQTT client disabled.");
		MqttConfig defaults;
		// Resetting to default values
		strcpy(mqttConfig.server, defaults.server);
		strcpy(mqttConfig.port, defaults.port);
		strcpy(mqttConfig.username, defaults.username);
		strcpy(mqttConfig.password, defaults.password);
		strcpy(mqttConfig.topic, defaults.topic);
	}
	else
	{
		// Setup MQTT publisher
		mqttClient.setup(mqttConfig);

		// Setup door opener
		doorOpener.setup();
	}

	server.on("/", [] { iotWebConf.handleConfig(); });
	server.onNotFound([]() { iotWebConf.handleNotFound(); });

	DEBUG("Setup done.");
}

void loop()
{
	// Mqtt client
	mqttClient.loop();
	yield();

	doorOpener.loop();
	yield();

	if (needReset)
	{
		// Doing a chip reset caused by config changes
		DEBUG("Rebooting after 1 second.");
		delay(1000);
		ESP.restart();
	}
	if (connected)
	{
		// Connected to wifi
		// yield();
	}
	iotWebConf.doLoop();
	yield();
}

void configSaved()
{
	DEBUG("Configuration was updated.");
	needReset = true;
}

void wifiConnected()
{
	DEBUG("WiFi connection established.");
	connected = true;
	mqttClient.connect();
}
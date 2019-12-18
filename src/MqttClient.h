#ifndef MQTTCLIENT_H
#define MQTTCLIENT_H

#include "config.h"
#include "debug.h"
#include "MQTT.h"
#include <ESP8266WiFi.h>

struct MqttConfig
{
    char server[128] = "mosquitto";
    char port[8] = "1883";
    char username[128];
    char password[128];
    char topic[128] = "iot/doorguard/";
};

class MqttClient
{
public:
    void setup(MqttConfig _config)
    {
        DEBUG("Setting up MQTT client.");
        config = _config;
        uint8_t lastCharOfTopic = strlen(config.topic) - 1;
        baseTopic = String(config.topic) + (lastCharOfTopic >= 0 && config.topic[lastCharOfTopic] == '/' ? "" : "/");

        client.begin(config.server, atoi(config.port), net);
        initialized = true;
    }

    void connect()
    {
        DEBUG("Establishing MQTT client connection.");
        client.connect("DoorGuard", config.username, config.password);
        if (client.connected())
        {
            char message[64];
            snprintf(message, 64, "Hello from %08X, running DoorGuard version %s.", ESP.getChipId(), VERSION);
            info(message);
        }
    }

    void loop()
    {
        client.loop();
    }

    void debug(const char *message)
    {
        publish(baseTopic + "debug", message);
    }

    void info(const char *message)
    {
        publish(baseTopic + "info", message);
    }

    void publish(const String &topic, const String &payload)
    {
        publish(topic.c_str(), payload.c_str());
    }
    void publish(String &topic, const char *payload)
    {
        publish(topic.c_str(), payload);
    }
    void publish(const char *topic, const String &payload)
    {
        publish(topic, payload.c_str());
    }
    void publish(const char *topic, const char *payload)
    {
        if (!initialized)
        {
            return;
        }

        if (!client.connected())
        {
            connect();
        }

        if (!client.connected())
        {
            // Something failed
            DEBUG("Connection to MQTT broker failed.");
            DEBUG("Unable to publish a message to '%s'.", topic);
            return;
        }

        DEBUG("Publishing message to '%s':", topic);
        DEBUG(payload);
        client.publish(topic, payload);
    }

private:
    MqttConfig config;
    WiFiClient net;
    MQTTClient client;
    bool connected = false;
    bool initialized = false;
    String baseTopic;
};

#endif
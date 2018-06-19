#!/usr/bin/python
import paho.mqtt.client as mqtt
import time
# The callback for when the client receives a CONNACK response from the server.
def on_connect(client, userdata, flags, rc):
    print("Connected with result code "+str(rc))

client = mqtt.Client()
client.on_connect = on_connect
client.connect("iot.eclipse.org",1883, 60)

while True:
        print("publish")
        time.sleep(5)
        client.publish("nielit","abcd")


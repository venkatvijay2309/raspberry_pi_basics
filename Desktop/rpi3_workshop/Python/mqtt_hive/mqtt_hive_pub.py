#!/usr/bin/python

#subscribe "nielit/#" topic in websock

import paho.mqtt.client as paho
import time

def on_connect(client, userdata, flags, rc):
    print("CONNACK received with code %d." % (rc))
	
def on_publish(client, userdata, mid):
    print("data published")

client = paho.Client()
client.on_connect = on_connect
client.on_publish = on_publish
client.connect("broker.mqttdashboard.com", 1883)
client.loop_start()

while True:
    temperature = int(open('/sys/class/thermal/thermal_zone0/temp').read()) / 1$
    (rc, mid) = client.publish("nielit", str(temperature), qos=1)
    time.sleep(1)

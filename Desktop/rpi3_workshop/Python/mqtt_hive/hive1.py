 import sys
 import paho.mqtt.client as mqtt

 def on_connect(mqttc, obj, flags, rc):
     print("rc: "+str(rc))

 def on_message(mqttc, obj, msg):
     print(msg.topic+" "+str(msg.qos)+" "+str(msg.payload))

 def on_publish(mqttc, obj, mid):
     print("mid: "+str(mid))

 def on_subscribe(mqttc, obj, mid, granted_qos):
     print("Subscribed: "+str(mid)+" "+str(granted_qos))

 def on_log(mqttc, obj, level, string):
     print(string)

 mqttc = mqtt.Client()   
 mqttc.on_message = on_message
 mqttc.on_connect = on_connect
 mqttc.on_publish = on_publish
 mqttc.on_subscribe = on_subscribe
 mqttc.connect("test.mosquitto.org", 8080, 60)
 mqttc.subscribe("test/iot", 0)

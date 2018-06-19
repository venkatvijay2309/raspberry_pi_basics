import time
import paho.mqtt.client as mqtt
message = 'Hello World!'
mqttc=mqtt.Client()
mqttc.connect("iot.eclipse.org",1883,60)
mqttc.loop_start()



while 1:
   
    mqttc.publish("nielitpi",message,2)
    time.sleep(1)

mqttc.loop_stop()
mqttc.disconnect()

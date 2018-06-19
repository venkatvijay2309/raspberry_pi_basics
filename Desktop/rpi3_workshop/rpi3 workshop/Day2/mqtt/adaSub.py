#!/usr/bin/python
# Import Adafruit IO MQTT client.
from Adafruit_IO import MQTTClient

ADAFRUIT_IO_KEY      = '7b1c2873a90d4c36b53ad4bc3795b781'
ADAFRUIT_IO_USERNAME = 'Nishantdawn'

def connected(client):
    # Connected function will be called when connected to Adafruit IO.
    print('Connected to Adafruit IO!...')
    client.subscribe('blight')

def message(client, feed_id, payload):
    # Message function will be called when a subscribed feed has a new value.
    # The feed_id parameter identifies the feed, and the payload parameter has
    # the new value.
    print('Feed {0} received new value: {1}'.format(feed_id, payload))


client = MQTTClient(ADAFRUIT_IO_USERNAME, ADAFRUIT_IO_KEY)
client.on_connect    = connected
client.on_message    = message
client.connect()
client.loop_blocking()

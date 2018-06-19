import time

# Import Adafruit IO MQTT client.
from Adafruit_IO import MQTTClient

ADAFRUIT_IO_KEY      = '7b1c2873a90d4c36b53ad4bc3795b781'
ADAFRUIT_IO_USERNAME = 'Nishantdawn'

def connected(client):
    # Connected function will be called when connected to Adafruit IO.
    print('Connected to Adafruit IO!...')


client = MQTTClient(ADAFRUIT_IO_USERNAME, ADAFRUIT_IO_KEY)
client.on_connect    = connected
client.connect()

while True:
    print('Publishing to Adafruit.')
    client.publish('rpi', 100)
    time.sleep(10)


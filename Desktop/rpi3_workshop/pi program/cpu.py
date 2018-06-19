#!/usr/bin/python
import time

while True:
        temp = int(open('/sys/class/thermal/thermal_zone0/temp').read()) / 1e3 # Get Raspberry Pi CPU temp
        print temp
        time.sleep(1)


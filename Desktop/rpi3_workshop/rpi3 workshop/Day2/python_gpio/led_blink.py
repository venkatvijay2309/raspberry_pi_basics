#!/usr/bin/python
import RPi.GPIO as GPIO
import time

GPIO.setmode(GPIO.BCM)  # set board mode to Broadcom
GPIO.setup(21, GPIO.OUT)  # set up pin 21

while True:
        
	GPIO.output(21, 1)  # turn on pin 21
        time.sleep(1)
	GPIO.output(21, 0)  # turn on pin 21
        time.sleep(1)

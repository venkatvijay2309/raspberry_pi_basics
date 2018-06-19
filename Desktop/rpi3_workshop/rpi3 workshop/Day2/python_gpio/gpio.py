#!/usr/bin/python
import RPi.GPIO as GPIO

GPIO.setmode(GPIO.BCM)  # set board mode to Broadcom
GPIO.setup(21, GPIO.OUT)  # set up pin 21
GPIO.output(21, 1)  # turn on pin 21

#!/usr/bin/python
import RPi.GPIO as GPIO
GPIO.setwarnings(False)
GPIO.setmode(GPIO.BCM)  # set board mode to Broadcom
GPIO.setup(21, GPIO.OUT)  # set up pin 21
while True:
        temp=raw_input("Enter Command: ")
        if temp == "on":
                GPIO.output(21, 1)
        if temp == "off":
                GPIO.output(21, 0)
        if temp == "exit":
                break


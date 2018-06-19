#!/usr/bin/python
import time
import RPi.GPIO as GPIO
GPIO.setmode(GPIO.BOARD)
GPIO.setwarnings(False)
GPIO.setup(12,GPIO.OUT)
for num in range(0,100):
	 GPIO.output(12,True)
         time.sleep(1)
         GPIO.output(12,False)
         time.sleep(1)









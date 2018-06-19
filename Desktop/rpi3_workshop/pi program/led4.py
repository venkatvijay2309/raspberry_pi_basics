#!/usr/bin/python
import time
import RPi.GPIO as GPIO
GPIO.setmode(GPIO.BOARD)
GPIO.setwarnings(False)
GPIO.setup(12,GPIO.OUT)
while True:
	pkt=raw_input('gpio control\n')
	if pkt== "1":
        	print "turn on"
                GPIO.output(12,True)
        else :
              if pkt== "0":
			print "turn off"  
                	GPIO.output(12,False)
        







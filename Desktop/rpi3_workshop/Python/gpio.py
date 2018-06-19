import RPi.GPIO as GPIO

GPIO.setmode(GPIO.BCM)  # set board mode to Broadcom
GPIO.setup(17, GPIO.OUT)  # set up pin 17
GPIO.output(17, 1)  # turn on pin 17

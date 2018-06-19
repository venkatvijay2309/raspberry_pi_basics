#!/usr/bin/python

# This code is written by Stephen C Phillips.
# It is in the public domain, so you can do what you like with it
# but a link to http://scphillips.com would be nice.

import socket
import re
import RPi.GPIO as GPIO
GPIO.setmode(GPIO.BCM)
GPIO.setup(21, GPIO.OUT)
# Standard socket stuff:
host = '192.168.20.xx' 
port = 8080
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1) #reuse port
sock.bind((host, port))
sock.listen(1) # don't queue up any requests

# Loop forever, listening for requests:
while True:
    csock, caddr = sock.accept()
    print "Connection from: " + `caddr`
    req = csock.recv(1024) # get the request, 1kB max
    print req
    match = re.match('GET /on HTTP/1.1', req)
    match1 = re.match('GET /off HTTP/1.1', req)
    if match:
	GPIO.output(21, 1)
        csock.sendall("""HTTP/1.0 200 OK
Content-Type: text/html

<html>
<head>
<title>Success</title>
</head>
<body>
Device is turned on!
</body>
</html>
""")
    if match1:
	GPIO.output(21, 0)
        csock.sendall("""HTTP/1.0 200 OK
Content-Type: text/html

<html>
<head>
<title>Success</title>
</head>
<body>
Device is turned off!
</body>
</html>
""")
    csock.close()

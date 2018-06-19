#!/usr/bin/python

import socket
import re

# Standard socket stuff:
host = '192.168.20.xx'
port = 8080
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1) #reuse port
sock.bind((host, port))
sock.listen(1)
# Loop forever, listening for requests:

while True:
    csock, caddr = sock.accept()
    print "Connection from: " + `caddr`
    req = csock.recv(1024) # get the request, 1kB max
    print req
    temp = int(open('/sys/class/thermal/thermal_zone0/temp').read()) / 1e3 # Get Raspberry Pi CPU temp
    csock.send('HTTP/1.0 200 OK\r\n')
    csock.send("Content-Type: text/html\r\n\r\n")
    csock.send('<html><body><h1>')
    csock.send(str(temp))
    csock.send('</body></html>')
    csock.close()


#!/usr/bin/python

# This code is written by Stephen C Phillips.
# It is in the public domain, so you can do what you like with it
# but a link to http://scphillips.com would be nice.

import socket
import re

# Standard socket stuff:
host = '192.168.137.xx' 
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
    # Look in the first line of the request for a on command
    # A on command should be e.g. 'http://server/on
    match = re.match('GET /on HTTP/1.1', req)
    if match:
        csock.sendall("""HTTP/1.0 200 OK
Content-Type: text/html

<html>
<head>
<title>Success</title>
</head>
<body>""")
		csock.sendall("""Device is turned on!""")
		csock.sendall("""</body>
</html>
""")
    else:
        # If there was no recognised command then return a 404 (page not found)
        print "Returning 404"
        csock.sendall("HTTP/1.0 404 Not Found\r\n")
    csock.close()

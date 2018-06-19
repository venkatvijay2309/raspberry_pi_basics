#!/usr/bin/python           # This is server.py file

import socket               # Import socket module

sock = socket.socket()         # Create a socket object
host = socket.gethostname() # Get local machine name
port = 12345                # Reserve a port for your service.
sock.bind((host, port))        # Bind to the port

sock.listen(5)                 # Now wait for client connection.
while True:
   csock, caddr = sock.accept()     # Establish connection with client.
   print 'Got connection from', caddr
   csock.send('Thank you for connecting')
   csock.close()                # Close the connection
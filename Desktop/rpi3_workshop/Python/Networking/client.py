#!/usr/bin/python           # This is client.py file

import socket               # Import socket module

sock = socket.socket()         # Create a socket object
host = socket.gethostname() # Get local machine name
port = 12345                # Reserve a port for your service.

sock.connect((host, port))
print sock.recv(1024)
sock.close 
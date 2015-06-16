import socket
import sys
import struct
import time

def new_sock():
  global port, addr
  sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
  sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
  sock.settimeout(None)
  sock.connect((addr, port))
  return sock

addr = sys.argv[1]
port = int(sys.argv[2])

sock = new_sock()
sock.sendall("load")
try:
  r = sock.recv(4)
  v = struct.unpack("!i", r)[0]
except socket.timeout:
  print "Received timeout"

print "load returned " + str(v)

sock.sendall("exit")
try:
  r = sock.recv(4)
  v = struct.unpack("!i", r)[0]
except socket.timeout:
  print "Received timeout"

print "exit returned " + str(v)

#!/bin/bash

# This is a Normal transaction completion. Used to produce ShiViz-Log.dat1

./cmd begin localhost 1111 localhost 1234 1
./cmd join localhost 1111 localhost 1234 1
./cmd join localhost 2222 localhost 1234 1
./cmd join localhost 3333 localhost 1234 1
./cmd join localhost 4444 localhost 1234 1
# start another transaction
./cmd begin localhost 5555 localhost 1234 2
./cmd join localhost 5555 localhost 1234 2
./cmd join localhost 6666 localhost 1234 2
sleep 2
# each worker modifies their datafile
./cmd newa localhost 1111 1111
./cmd newa localhost 2222 2222
./cmd newb localhost 3333 3333
./cmd newid localhost 4444 imnode4444

./cmd newa localhost 5555 5555
./cmd newb localhost 6666 6666

sleep 4
./cmd commit localhost 1111
./cmd commit localhost 5555
sleep 5

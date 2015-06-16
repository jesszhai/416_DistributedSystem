#!/bin/bash

# This is the situation where worker crash after the response (vote_commit) is sent, 
# and then the manager also crashed after making the decision (decided to commit).
# restarting worker and manager, the final TX result is to commit.

# This script is used to produce ShiViz-Log.dat2
./cmd begin localhost 1111 localhost 1234 1
./cmd join localhost 1111 localhost 1234 1
./cmd join localhost 2222 localhost 1234 1
./cmd join localhost 3333 localhost 1234 1
./cmd join localhost 4444 localhost 1234 1
sleep 2
# each worker modifies their datafile
./cmd newa localhost 1111 1111
./cmd newa localhost 2222 2222
./cmd newb localhost 3333 3333
./cmd newid localhost 4444 imnode4444
./cmd delay localhost 1111 -1  # Crash worker after the respond is sent 
sleep 4
./cmd commitcrash localhost 2222  # request a commit, and then manager logs the dicision and then manager crash
sleep 5

# manually restart tworker 1111
# manually restart manager
# result should be commit
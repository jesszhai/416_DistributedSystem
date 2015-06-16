#!/bin/bash
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
./cmd commitcrash localhost 1111
# coord log the decision to commit, then crash
# restart manager
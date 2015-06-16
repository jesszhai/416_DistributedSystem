#!/bin/bash
set -v
if [ "$#" -ne 9 ]; then
	echo "Usage: $0 WORKER_HOST WORKER_PORT TX_MANAGER_HOST TX_MANAGER_PORT TID NEW_A NEW_B NEW_ID DELAY"
	exit 1
fi

./cmd
./cmd begin
./cmd join
./cmd newa
./cmd newb
./cmd newid
./cmd delay
./cmd crash 
./cmd commit
./cmd commitcrash 
./cmd abort
./cmd abortcrash
./cmd voteabort
./cmd begin $1 $2 $3
./cmd join $1 $2 $3
./cmd newa $1 $2
./cmd newb $1 $2
./cmd newid $1 $2
./cmd delay $1 $2
./cmd begin $1 $2 $3 $4 $5
./cmd join $1 $2 $3 $4 $5
./cmd newa $1 $2 $6
./cmd newb $1 $2 $7
./cmd newid $1 $2 $8
./cmd delay $1 $2 $9
./cmd crash $1 $2
./cmd commit $1 $2
./cmd commitcrash $1 $2
./cmd abort $1 $2
./cmd abortcrash $1 $2
./cmd voteabort $1 $2

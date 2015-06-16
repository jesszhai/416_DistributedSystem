#ifndef TMANAGER_H
#define TMANAGER_H 1

#include "tworker.h"

// specific worker information that is in the transaction
struct w {
    unsigned int nodeId;
    uint32_t   port;
    struct sockaddr_in workerAddr;
    uint32_t   voteYes;
};

// information for single transaction
struct info {
  uint32_t     tid;
  int          isCrash;
  struct w workers[MAX_NODES];
};

// information for all transactions managed by manager
struct CoordCrashInfo{
  struct clock vectorClock[MAX_NODES];
  struct info workerInfo[MAX_TRANS];
};

#endif
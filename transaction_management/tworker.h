
#ifndef TWORKER_H
#define TWORKER_H 1
#include <sys/time.h>

#define MAX_NODES 10
#define MAX_TRANS 5
#define IDLEN     64
/* A clock value consists of the node number (i.e. a node's port
   number) and the clock value. 
*/


struct clock {
  unsigned int nodeId;
  unsigned int time;
};

struct clock   vectorClock[MAX_NODES];


typedef     /* got the port number create a logfile name */
struct {
  char IDstring[IDLEN];   // This must be a null terminated ASCII string
  struct timeval lastUpdateTime;
  int A;
  int B;
  struct clock vectorClock[MAX_NODES];
} ObjectData;

#endif /* TWORKER_H */

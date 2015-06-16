
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include "msg.h"
#include "tworker.h"
#include "helper.h"
#include "tmanager.h"

void usage(char * cmd) {
  printf("usage: %s  portNum\n",
	 cmd);
}


int main(int argc, char ** argv) {

  // This is some sample code feel free to delete it
  
  unsigned long  port;
  char           dataObjectFileName[128];
  char           timeBuf[128];
  int            dataObjectFD;
  ObjectData     *objData;

  char           crashInfoFileName[128];
  int            crashInfoFD;
  CrashInfo      *crashInfo;

  int retVal;
  struct stat    fstatus;

  if (argc != 2) {
    usage(argv[0]);
    return -1;
  }

  char * end;
  int err = 0;

  port = strtoul(argv[1], &end, 10);
  if (argv[1] == end) {
    printf("Port conversion error\n");
    return -1;
  } 
  
  snprintf(dataObjectFileName, sizeof(dataObjectFileName), 
	   "WorkerData_%d.data", port);
  
  dataObjectFD = open(dataObjectFileName, O_RDONLY);
  if (dataObjectFD < 0 ) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Opening %s failed", dataObjectFileName);
    perror(msg);
    
  } else {
    if(fstat(dataObjectFD, &fstatus) < 0 ) {
      perror("Filestat failed");
      return -3;
    }

    if (fstatus.st_size != sizeof(ObjectData)) {
        /* File hasn't been mapped in before 
           so we need to make sure there is enough
           space used in the file to hold 
           the data.
        */
      printf("The object data file has an invalid size\n");
    }

    
    /* We could simply read the file but I am doing an mmap 
       just as an example.
    */

      objData = mmap(NULL, 512, PROT_READ,
  		 MAP_PRIVATE, dataObjectFD, 0);
    
    if (objData == 0) {
      perror("Object data could not be mapped in");
      return -1;
    }
    
    
    printf("String ID: %s\n", objData->IDstring);
    time_t p;
    p = objData->lastUpdateTime.tv_sec;
    char *timebuff = ctime(&p);
    timebuff[strlen(timebuff)-1] = '\0';

    printf(" %s  usec %d\n", timebuff,
    	 objData->lastUpdateTime.tv_usec);
    printf(" Object A: %15d 0x%08x\n", objData->A, objData->A);
    printf(" Object B: %15d 0x%08x\n", objData->B, objData->B);

    // Print the vectorClock field

    int i;
    
    for (i = 0; i < MAX_NODES; i++) {
      printf("    node[%d] = %u\n",  objData->vectorClock[i].nodeId,
  	   objData->vectorClock[i].time);
    }
  }

  // Dump tworker transaction log
  snprintf(crashInfoFileName, sizeof(crashInfoFileName), 
     "WorkerLog_%d.log", port);
  
  crashInfoFD = open(crashInfoFileName, O_RDONLY);
  if (crashInfoFD < 0 ) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Opening %s failed", crashInfoFileName);
    perror(msg);
  } else {
    if(fstat(crashInfoFD, &fstatus) < 0 ) {
      perror("Filestat failed");
      return -3;
    }

    if (fstatus.st_size < sizeof(CrashInfo)) {
      printf("The crash info file has an invalid size\n");
    }

    crashInfo = mmap(NULL, 512, PROT_READ, MAP_PRIVATE, crashInfoFD, 0);
    if (crashInfo == 0) {
      perror("Crash Info could not be mapped in");
      return -1;
    }
    
    printf("System Port: %lu\n", crashInfo->sys_port);
    int j;
    for (j = 0; j < MAX_NODES; j++) {
      printf("    node[%d] = %u\n",  crashInfo->vectorClock[j].nodeId,
       crashInfo->vectorClock[j].time);
    }

    Record * rec = (Record *) (crashInfo + 1);

    int numRec = (fstatus.st_size - sizeof(CrashInfo))/sizeof(Record);
    int k;
    for (k = 0; k < numRec; k++) {
      Record * r = rec + k;
      printRecord(*r);
    }
    close(crashInfoFD);
  }

  // Dump tmanager transaction log
  snprintf(crashInfoFileName, sizeof(crashInfoFileName), 
     "TmanagerLog_%d.log", port);
  crashInfoFD = open(crashInfoFileName, O_RDONLY);
  if (crashInfoFD < 0 ) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Opening %s failed", crashInfoFileName);
    perror(msg);
  } else {
    if(fstat(crashInfoFD, &fstatus) < 0 ) {
      perror("Filestat failed");
      return -3;
    }

    if (fstatus.st_size < sizeof(CrashInfo)) {
      printf("The crash info file has an invalid size\n");
    }

    struct CoordCrashInfo * coordCrashInfo;
    coordCrashInfo = mmap(NULL, 512, PROT_READ, MAP_PRIVATE, crashInfoFD, 0);
    if (coordCrashInfo == 0) {
      perror("Crash Info could not be mapped in");
      return -1;
    }

    CoordRecord * rec = (CoordRecord *) (coordCrashInfo + 1);

    int numRec = (fstatus.st_size - sizeof(struct CoordCrashInfo))/sizeof(CoordRecord);
    int k;
    for (k = 0; k < numRec; k++) {
      CoordRecord * r = rec + k;
      printCoordRecord(*r);
    }
    close(crashInfoFD);
  }
}

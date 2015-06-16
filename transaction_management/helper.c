#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "msg.h"

/*
  Write buffer to the specified file with logFileName
*/
void writeToFile(char * logFileName, char * buf) {
  FILE * fp;
  fp = fopen(logFileName, "a");
  if (fp == NULL) {
    perror("fopen() failed");
    exit(1);
  }
  if (fputs(buf, fp) == EOF) {
    perror("fputs() failed");
    exit(1);
  }
  fclose(fp);
}

/*
  Write node start event to ShiViz log 
*/
void writeStartToLog(char * logFileName, unsigned long port, unsigned long clock) {
  char logBuf[100];
  sprintf(logBuf, "Starting N%d\nN%d {\"N%d\" : %d}\n", port, port, port, clock);
  writeToFile(logFileName, logBuf);
}

/*
  Create and socket given the port
*/
int createSocket(unsigned long port) {
  struct sockaddr_in local_addr;
  int sock;
  // Create socket for sending/receiving datagrams
  if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
    perror("socket() failed");
    return -1;
  }

  // Construct local address structure
  memset(&local_addr, 0, sizeof(local_addr));     /* Zero out structure */
  local_addr.sin_family = AF_INET;                /* Internet address family */
  local_addr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
  local_addr.sin_port = htons(port);        /* Local port */

  // Bind to the local address
  if (bind(sock, (struct sockaddr *) &local_addr, sizeof(local_addr)) < 0) {
    perror("bind() failed");
    return -1;
  }
  return sock;
}

/*
  Create Socket for cmd program
  Port is determined by system
*/
int createCmdSocket() {
  struct sockaddr_in local_addr;
  int sock;
  // Create socket for sending/receiving datagrams
  if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
    perror("socket() failed");
    return -1;
  }
  return sock;
}

/*
  SERIALIZATION METHODS FOR MESSAGE
*/
char * serializeInt(char * buffer, int32_t val) {
  uint32_t temp = htonl(val);
  memcpy(buffer, &temp, sizeof(int32_t));
  return buffer + sizeof(int32_t);
}

char * serializeUnsignedInt(char * buffer, uint32_t val) {
  uint32_t temp = htonl(val);
  memcpy(buffer, &temp, sizeof(uint32_t));
  return buffer + sizeof(uint32_t);
}

void serializeMsg(char * buffer, msgType m) {
  buffer = serializeUnsignedInt(buffer, m.msgID);
  buffer = serializeUnsignedInt(buffer, m.tid);
  buffer = serializeUnsignedInt(buffer, m.port);
  buffer = serializeInt(buffer, m.newValue);
  buffer = serializeInt(buffer, m.delay);
  if (m.msgID == NEW_IDSTR) {
    memcpy(buffer, (m.strData).newID, IDLEN);
  } else {
    memcpy(buffer, (m.strData).hostName, HOSTLEN);
  }
}

char * serializeClock(char * buffer, struct clock * vc) {
  int i;
  for (i=0; i < MAX_NODES; i++) {
    buffer = serializeInt(buffer, vc[i].nodeId);
    buffer = serializeInt(buffer, vc[i].time);
  }
  return buffer;
}

void serializeWorkerMsg(char * buffer, txMessage m){
  buffer = serializeUnsignedInt(buffer, m.msgID);
  buffer = serializeUnsignedInt(buffer, m.tid);
  buffer = serializeUnsignedInt(buffer, m.nodeId);
  buffer = serializeUnsignedInt(buffer, m.isCrash);
  buffer = serializeClock(buffer, m.vectorClock);
}

/*
  DESERIALIZATION METHODS FOR MESSAGE
*/
char * deserializeInt(char * buffer, int32_t * val) {
  uint32_t temp;
  memcpy(&temp, buffer, sizeof(uint32_t));
  buffer = buffer + sizeof(uint32_t);
  *val = ntohl(temp);
  return buffer;
}

char * deserializeUnsignedInt(char * buffer, uint32_t * val) {
  uint32_t temp;
  memcpy(&temp, buffer, sizeof(uint32_t));
  buffer = buffer + sizeof(uint32_t);
  *val = ntohl(temp);
  return buffer;
}

msgType deserializeMsg(char * buffer) {
  msgType msg;
  buffer = deserializeUnsignedInt(buffer, &msg.msgID);
  buffer = deserializeUnsignedInt(buffer, &msg.tid);
  buffer = deserializeUnsignedInt(buffer, &msg.port);
  buffer = deserializeInt(buffer, &msg.newValue);
  buffer = deserializeInt(buffer, &msg.delay);
  if (msg.msgID == NEW_IDSTR) {
    memcpy(msg.strData.newID, buffer, IDLEN);
  } else {
    memcpy(msg.strData.hostName, buffer, HOSTLEN);
  }
  return msg;
}

char * deserializeClockUnsignedInt(char * buffer, unsigned int * val) {
  uint32_t temp;
  memcpy(&temp, buffer, sizeof(unsigned int));
  buffer = buffer + sizeof(unsigned int);
  *val = ntohl(temp);
  return buffer;
}

char * deserializeClock(char * buffer, txMessage * r) {
  int i;
  for (i=0; i < MAX_NODES; i++) {
    unsigned int val;
    buffer = deserializeClockUnsignedInt(buffer, &val);
    r->vectorClock[i].nodeId = val;
    buffer = deserializeClockUnsignedInt(buffer, &val);
    r->vectorClock[i].time = val;
  }
  return buffer;
}

txMessage deserializeWorkerMsg(char * buffer) {
  txMessage msg;
  buffer = deserializeUnsignedInt(buffer, &msg.msgID);
  buffer = deserializeUnsignedInt(buffer, &msg.tid);
  buffer = deserializeUnsignedInt(buffer, &msg.nodeId);
  buffer = deserializeUnsignedInt(buffer, &msg.isCrash);
  buffer = deserializeClock(buffer, &msg);
  return msg;
}

/*
  Debug print method for messages between cmd and tworker
*/
void printMsg(msgType m) {
  printf("msgType: %u\n", m.msgID);
  printf("tid: %u\n", m.tid);
  printf("port: %u\n", m.port);
  printf("newValue: %d\n", m.newValue);
  printf("delay: %d\n", m.delay);
  printf("newID: %s\n", (m.strData).newID);
  printf("hostName: %s\n", (m.strData).hostName);
}

/*
  Debug print method for messages between tworker and tmanager
*/
void printWorkerMsg(txMessage m) {
  printf("msgType: %u\n", m.msgID);
  printf("tid: %u\n", m.tid);
  printf("isCrash: %d\n", m.isCrash);
  int i;
  for (i=0; i < MAX_NODES; i++) {
    printf("vectorClock[%d].nodeId: %u\n", i, m.vectorClock[i].nodeId);
    printf("vectorClock[%d].time: %u\n", i, m.vectorClock[i].time);
  }
}

/*
  Debug print method for tworker Record
*/
void printRecord (Record r) {
  printf("msgType: %u\n", r.msgID);
  printf("tid: %u\n", r.tid);
  printf("oldValue: %d\n", r.oldValue);
  printf("oldID: %s\n", r.oldID);
  time_t p;
  p = r.lastUpdateTime.tv_sec;
  char *timebuff = ctime(&p);
  timebuff[strlen(timebuff)-1] = '\0';

  printf(" %s  usec %d\n", timebuff,
     r.lastUpdateTime.tv_usec);

  int i;
  
  for (i = 0; i < MAX_NODES; i++) {
    printf("    node[%d] = %u\n",  r.vectorClock[i].nodeId,
     r.vectorClock[i].time);
  }
}

/*
  Debug print method for tmanager Record
*/
void printCoordRecord (CoordRecord r) {
  printf("msgType: %u\n", r.msgID);
  printf("tid: %u\n", r.tid);
  printf("nodeId: %u\n", r.nodeId);
}

/*
  Increment the node's own vector clock
*/
int updateMyVectorClock(unsigned long port, struct clock * vc) {
  int i;
  for (i=0; i< MAX_NODES; i++) {
    if (vc[i].nodeId == port) {
      vc[i].time++;
      break;
    }
  }
  return i;
}

/*
  Format and write the vector clock to buffer for logging
*/
void writeVectorTime(char * vectorTimeBuf, struct clock * vc, unsigned long port) {
  int i;
  char nodeIdBuf[10];
  sprintf(nodeIdBuf, "N%lu {", port);
  strcpy(vectorTimeBuf, nodeIdBuf);
  for (i = 0; i < MAX_NODES; i++) {
    if (vc[i].time != 0) {
      char nodeTimeBuf[20]; 
      sprintf(nodeTimeBuf, "\"N%u\" : %u,", vc[i].nodeId, vc[i].time);
      strcat(vectorTimeBuf, nodeTimeBuf);
    }
  }
  vectorTimeBuf[strlen(vectorTimeBuf) - 1] = '\0';
  strcat(vectorTimeBuf, "}\n");
}

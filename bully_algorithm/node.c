
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h> 
#include <sys/time.h>
#include "msg.h"

pthread_t * threadPtr = NULL;         // pointer to AYA thread ID
pthread_t threadID;                   // treadID of AYA thread
pthread_mutex_t myVectorClock_lock;   // mutex lock for myVectorClock
pthread_mutex_t logFile_lock;         // mutex lock for writing to log file

unsigned long AYATime;                          // AYA send rate
unsigned int counter;                 // counter for unique electionID
uint32_t numOfMembers;                // number of members in group
unsigned long timeoutValue;           // timeout value
unsigned long sendFailureProbability; // probability of packet being dropped
msgType lastSent;                     // state: last message sent by this node
msgType lastReceived;                 // state: last message received by this node

struct clock myVectorClock[MAX_NODES]; // node's vector clock

// struct of group member information
struct groupMem {
  char hostname[32];
  unsigned long portno;
};

// arguments to thread start method
// referenced from http://cs.ecs.baylor.edu/~donahoo/practical/CSockets/code/TCPEchoServer-Thread.c
struct ThreadArgs {
    int socket;
    unsigned long port;
    struct sockaddr_in from_addr;
    char * logFileName;

};

void writeEventToLog(char * logFileName, struct msg * m, unsigned long port, int isSend, unsigned long toFromNodeId);
void mergeVectorClock(struct msg* m);
void writeToFile(char * logFileName, char * buf);
void handleMsg(unsigned long port, struct msg * recMsg, struct msg * newMsg, int sock, struct sockaddr_in from_addr, char * logFileName, struct groupMem * groupMems);
void updateMyVectorClock(unsigned long port);
void constructMsg(msgType msgID, unsigned int electionID, unsigned long nodeId, struct msg* m);
void *ThreadMain(void *arg);

/*
  Print program usage
*/
void usage(char * cmd) {
  printf("usage: %s  portNum groupFileList logFile timeoutValue averageAYATime failureProbability \n",
    cmd);
}

/*
  If filename specified for group file, read group members from file
  otherwise if '-' read from standard input
*/
int readGroupListFile(char * fileName, struct groupMem* groupMems) {
  FILE *fp;
  numOfMembers = 0;

  // if '-', read from standard input
  if (*fileName == '-') {
    printf("Please input the group list: ");
    while(scanf("%s %ld", groupMems[numOfMembers].hostname, &(groupMems[numOfMembers].portno)) != EOF) {
      numOfMembers++;
      printf("Please input the group list: ");
    }
    return 1;
  }

  // try open the file
  fp = fopen(fileName,"r");
  if(fp == NULL) {
    perror("Error in opening file");
    return -1;
  }

  // read the hostname and port number pair
  while (fscanf(fp, "%s %ld", groupMems[numOfMembers].hostname, &(groupMems[numOfMembers].portno)) != -1) {
    numOfMembers++;
  }
  fclose(fp);
  return 1;
}

/*
  Check if this node is part of the group
*/
int foundInGroup(unsigned long port, struct groupMem* groupMems) {
  int i;
  for (i=0; i < numOfMembers; i++) {
    if(port == groupMems[i].portno){
      return i;
    }
  }
  return -1;
}

/* 

  MESSAGE SERIALIZATION HELPER METHODS 

*/

unsigned char * serializeInt(unsigned char * buffer, unsigned int val) {
  uint32_t temp = htonl(val);
  memcpy(buffer, &temp, sizeof(uint32_t));
  return buffer + sizeof(uint32_t);
}

unsigned char * serializeClock(unsigned char * buffer, struct clock * vc) {
  int i;
  for (i=0; i < MAX_NODES; i++) {
    buffer = serializeInt(buffer, vc[i].nodeId);
    buffer = serializeInt(buffer, vc[i].time);
  }
  return buffer;
}

void serializeMsg(unsigned char * buffer, struct msg * m) {
  buffer = serializeInt(buffer, m->msgID);
  buffer = serializeInt(buffer, m->electionID);
  buffer = serializeClock(buffer, m->vectorClock);
}

/*

  MESSAGE DESERIALIZATION HELPER METHODS

*/
unsigned char * deserializeInt(unsigned char * buffer, unsigned int * val) {
  uint32_t temp;
  memcpy(&temp, buffer, sizeof(uint32_t));
  buffer = buffer + sizeof(uint32_t);
  *val = ntohl(temp);
  return buffer;
}

unsigned char * deserializeMsgID(unsigned char * buffer, struct msg * r) {
  unsigned int val;
  buffer = deserializeInt(buffer, &val);
  r->msgID = (msgType) val;
  return buffer;
}

unsigned char * deserializeClock(unsigned char * buffer, struct msg * r) {
  int i;
  for (i=0; i < MAX_NODES; i++) {
    unsigned int val;
    buffer = deserializeInt(buffer, &val);
    r->vectorClock[i].nodeId = val;
    buffer = deserializeInt(buffer, &val);
    r->vectorClock[i].time = val;
  }
  return buffer;
}

void deserializeMsg(unsigned char * buffer, struct msg* r) {
  buffer = deserializeMsgID(buffer, r);
  unsigned int val;
  buffer = deserializeInt(buffer, &val);
  r->electionID = val;
  buffer = deserializeClock(buffer, r);
}

/*
  Debug method to print message content
*/
void printMsg(struct msg* m) {
  printf("msgType: %d\n", m->msgID);
  printf("electionID: %u\n", m->electionID);
  int i;
  for (i=0; i < MAX_NODES; i++) {
    printf("vectorClock[%d].nodeId: %u\n", i, m->vectorClock[i].nodeId);
    printf("vectorClock[%d].time: %u\n", i, m->vectorClock[i].time);
  }
}

/*
  Serialize the message and send as UDP packet if generated number is greater than sendFailureProbability
  Otherwise the package is "dropped" and not sent
*/
int sendMsg(int sock, struct msg * m, struct sockaddr_in from_addr) {
  unsigned int fromAddrLen = sizeof(from_addr);
  unsigned char msgBuf[sizeof(struct msg)];
  int rn = random() % 100;
  if (rn >= sendFailureProbability) {
    serializeMsg(msgBuf, m);
    if (sendto(sock, msgBuf, sizeof(struct msg), 0, (struct sockaddr *) &from_addr, fromAddrLen) != sizeof(struct msg)){
      perror("sendto() sent a different number of bytes than expected");
      return -1;
    }
  }
}

/*
  Initiate the sending of messages
  Handles the initial startup of the nodes in the group
  Continually check for new messages received on socket
*/
int sendUDPMsg(unsigned long local_port, char * logFileName, struct groupMem * groupMems) {

  int sock;                       /* Socket */
  struct sockaddr_in local_addr;  /* Local(my) address */
  struct sockaddr_in to_addr;    /* Receiver address */
  unsigned int toAddrLen;        /* Length of incoming message */
  char to_host[10];
  unsigned long to_portno;

  struct sockaddr_in from_addr;    /* Receiver address */
  unsigned int fromAddrLen;        /* Length of incoming message */
  char from_host[10];
  unsigned long from_portno;

  char recvMsg[512];              /* Buffer for the message */
  char* msg;
  int recvMsgSize;
  int msgLength;
  struct hostent *to_ip;

  int handleFirstMsg = 0;
  int i;

  // Create socket for sending/receiving datagrams
  if ((sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
    perror("socket() failed");
    return -1;
  }

  // Construct local address structure
  memset(&local_addr, 0, sizeof(local_addr));     /* Zero out structure */
  local_addr.sin_family = AF_INET;                /* Internet address family */
  local_addr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
  local_addr.sin_port = htons(local_port);        /* Local port */

  // Bind to the local address
  if (bind(sock, (struct sockaddr *) &local_addr, sizeof(local_addr)) < 0) {
    perror("bind() failed");
    return -1;
  }

  struct timeval tv;
  tv.tv_sec = timeoutValue;
  tv.tv_usec = 0;
  if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,&tv,sizeof(tv)) < 0) {
    perror("Error");
  }
  fromAddrLen = sizeof(from_addr);
  if ((recvMsgSize = recvfrom(sock, recvMsg, 512, 0, (struct sockaddr *) &from_addr, &fromAddrLen)) < 0) {
    for (i = 0; i < numOfMembers; i++) {
      struct sockaddr_in mem_addr;
      struct hostent *mem_ip;

      lastReceived = ELECT;
      lastSent = 0;
      if (groupMems[i].portno > local_port) {
        mem_ip = gethostbyname(groupMems[i].hostname);
        if (mem_ip == NULL) {
          fprintf(stderr,"ERROR, no such host\n");
          exit(0);
        }
        memset(&mem_addr, 0, sizeof(mem_addr));
        mem_addr.sin_family = AF_INET;
        bcopy((char *)mem_ip->h_addr, 
              (char *)&mem_addr.sin_addr,
              mem_ip->h_length);
        mem_addr.sin_port = htons(groupMems[i].portno);
        struct msg electMsg;
        updateMyVectorClock(local_port);
        constructMsg(ELECT, counter++, local_port, &electMsg);
        sendMsg(sock, &electMsg, mem_addr);
        writeEventToLog(logFileName, &electMsg, local_port, 1, ntohs(mem_addr.sin_port)); 
        lastSent = ELECT;
      }
    }
  } else {
    lastReceived = 0;
    lastSent = 0;
    handleFirstMsg = 1;
  }

  // =============== receive msg ============== //
  for (;;)
  {
    // Recv a response 
    if (handleFirstMsg == 0) {
      fromAddrLen = sizeof(from_addr);
      if ((recvMsgSize = recvfrom(sock, recvMsg, 512, 0, (struct sockaddr *) &from_addr, &fromAddrLen)) < 0){
        struct msg newElectMsg;
        switch (lastSent) {
          case ELECT:
            for (i = 0; i < numOfMembers; i++) {
              struct sockaddr_in mem_addr;
              struct hostent *mem_ip;

              if (groupMems[i].portno < local_port) {
                mem_ip = gethostbyname(groupMems[i].hostname);
                if (mem_ip == NULL) {
                  fprintf(stderr,"ERROR, no such host\n");
                  exit(0);
                }
                memset(&mem_addr, 0, sizeof(mem_addr));
                mem_addr.sin_family = AF_INET;
                bcopy((char *)mem_ip->h_addr, 
                      (char *)&mem_addr.sin_addr,
                      mem_ip->h_length);
                mem_addr.sin_port = htons(groupMems[i].portno);

                updateMyVectorClock(local_port);
                constructMsg(COORD, 0, local_port, &newElectMsg);
                sendMsg(sock, &newElectMsg, mem_addr);
                writeEventToLog(logFileName, &newElectMsg, local_port, 1, ntohs(mem_addr.sin_port)); 
                lastSent = COORD;
              }
            }
            break;
          case AYA:
            // IAA not received from COORD
            // cancel thread and stop sending of AYA messages
            // start a new election
            if (pthread_cancel(*threadPtr) != 0) {
              perror("pthread_cancel() failed");
              exit(1);
            }
            threadPtr = NULL;

            for (i = 0; i < numOfMembers; i++) {
              struct sockaddr_in mem_addr;
              struct hostent *mem_ip;

              if (groupMems[i].portno > local_port) {
                mem_ip = gethostbyname(groupMems[i].hostname);
                if (mem_ip == NULL) {
                  fprintf(stderr,"ERROR, no such host\n");
                  exit(0);
                }
                memset(&mem_addr, 0, sizeof(mem_addr));
                mem_addr.sin_family = AF_INET;
                bcopy((char *)mem_ip->h_addr, 
                  (char *)&mem_addr.sin_addr,
                  mem_ip->h_length);
                mem_addr.sin_port = htons(groupMems[i].portno);
                struct msg electMsg;
                updateMyVectorClock(local_port);
                constructMsg(ELECT, counter++, local_port, &electMsg);
                sendMsg(sock, &electMsg, mem_addr);
                writeEventToLog(logFileName, &electMsg, local_port, 1, ntohs(mem_addr.sin_port));
                lastSent = ELECT; 
              }
            }
            break;
          case 0:
            if (lastReceived == ANSWER) {
              for (i = 0; i < numOfMembers; i++) {
                struct sockaddr_in mem_addr;
                struct hostent *mem_ip;

                if (groupMems[i].portno > local_port) {
                  mem_ip = gethostbyname(groupMems[i].hostname);
                  if (mem_ip == NULL) {
                    fprintf(stderr,"ERROR, no such host\n");
                    exit(0);
                  }
                  memset(&mem_addr, 0, sizeof(mem_addr));
                  mem_addr.sin_family = AF_INET;
                  bcopy((char *)mem_ip->h_addr, 
                    (char *)&mem_addr.sin_addr,
                    mem_ip->h_length);
                    mem_addr.sin_port = htons(groupMems[i].portno);
                  struct msg electMsg;
                  updateMyVectorClock(local_port);
                  constructMsg(ELECT, counter++, local_port, &electMsg);
                  sendMsg(sock, &electMsg, mem_addr);
                  writeEventToLog(logFileName, &electMsg, local_port, 1, ntohs(mem_addr.sin_port)); 
                  lastSent = ELECT;
                }
              }
            }

            if (lastReceived == ELECT) {
              for (i = 0; i < numOfMembers; i++) {
                struct sockaddr_in mem_addr;
                struct hostent *mem_ip;

                if (groupMems[i].portno < local_port) {
                  mem_ip = gethostbyname(groupMems[i].hostname);
                  if (mem_ip == NULL) {
                    fprintf(stderr,"ERROR, no such host\n");
                    exit(0);
                  }
                  memset(&mem_addr, 0, sizeof(mem_addr));
                  mem_addr.sin_family = AF_INET;
                  bcopy((char *)mem_ip->h_addr, 
                    (char *)&mem_addr.sin_addr,
                    mem_ip->h_length);
                  mem_addr.sin_port = htons(groupMems[i].portno);

                  updateMyVectorClock(local_port);
                  constructMsg(COORD, 0, local_port, &newElectMsg);
                  sendMsg(sock, &newElectMsg, mem_addr);
                  writeEventToLog(logFileName, &newElectMsg, local_port, 1, ntohs(mem_addr.sin_port)); 
                  lastSent = COORD;
                }
              }
            }
            break;
          case IAA:
            break;
          case COORD:
            break;
          default:
            break;
        }
      }
    }
    if (recvMsgSize > 0) {
      handleFirstMsg = 0;
      struct msg rec;
      deserializeMsg(recvMsg, &rec);

      msgType msgID = rec.msgID;

      mergeVectorClock(&rec);
      updateMyVectorClock(local_port);
      writeEventToLog(logFileName, &rec, local_port, 0,ntohs(from_addr.sin_port));
      struct msg newMsg;
      handleMsg(local_port, &rec, &newMsg, sock, from_addr, logFileName, groupMems);
    }
  }
}

/*
  Helper method to merge the node's vector clock with the received message's vector
*/
void mergeVectorClock(struct msg* m){
  int i;
  for (i=0; i< MAX_NODES; i++) {
    if(m->vectorClock[i].time > myVectorClock[i].time){
      myVectorClock[i].nodeId = m->vectorClock[i].nodeId;
      myVectorClock[i].time = m->vectorClock[i].time;
    }
  }
}

/*
  Increment the node's own vector clock
*/
void updateMyVectorClock(unsigned long port) {
  int i;
  pthread_mutex_lock(&myVectorClock_lock);
  for (i=0; i< MAX_NODES; i++) {
    if (myVectorClock[i].nodeId == port) {
      myVectorClock[i].time++;
    }
  }
  pthread_mutex_unlock(&myVectorClock_lock);
}

/*
  Write the given buffer to the logfile
*/
void writeToFile(char * logFileName, char * buf) {
  FILE * fp;
  pthread_mutex_lock(&logFile_lock);
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
  pthread_mutex_unlock(&logFile_lock);
}

/*
  Construct vector timestamp of even to be logged
*/
void writeVectorTime(char * vectorTimeBuf, unsigned long port) {
  int i;
  char nodeIdBuf[6];
  sprintf(nodeIdBuf, "N%lu {", port);
  strcpy(vectorTimeBuf, nodeIdBuf);
  for (i = 0; i < MAX_NODES; i++) {
    if (myVectorClock[i].time != 0) {
      char nodeTimeBuf[20]; 
      sprintf(nodeTimeBuf, "\"N%u\" : %u,", myVectorClock[i].nodeId, myVectorClock[i].time);
      strcat(vectorTimeBuf, nodeTimeBuf);
    }
  }
  vectorTimeBuf[strlen(vectorTimeBuf) - 1] = '\0';
  strcat(vectorTimeBuf, "}\n");
}

/*
  Construct description of event to be logged
*/
void writeEventToLog(char * logFileName, struct msg * m, unsigned long port, int isSend, unsigned long toFromNodeId) {
  char logBuf[500];
  char vectorTimeBuf[500];
  char portBuf[10];
  switch (m->msgID) {
    case ELECT:
      sprintf(logBuf, isSend ? "Send ELECTION %lu to " : "Receive ELECTION %lu from ", m->electionID);
      break;
    case ANSWER:
      strcpy(logBuf, isSend ? "Send ANSWER to " : "Receive ANSWER from ");
      break;
    case COORD:
      strcpy(logBuf, isSend ? "Send COORD to " : "Receive COORD from ");
      break;
    case AYA:
      strcpy(logBuf, isSend ? "Send AYA to " : "Receive AYA from ");
      break;
    case IAA:
      strcpy(logBuf, isSend ? "Send IAA to " : "Receive IAA from ");
      break;
    default:
      break;
  }
  sprintf(portBuf, "N%lu", toFromNodeId);
  strcat(logBuf, portBuf);
  strcat(logBuf, "\n");
  writeVectorTime(vectorTimeBuf, port);
  strcat(logBuf, vectorTimeBuf);
  writeToFile(logFileName, logBuf);
}

/*
  Log the node startup event
*/
void writeStartToLog(char * logFileName, unsigned long port, unsigned long clock) {
  char logBuf[100];
  sprintf(logBuf, "Starting N%d\nN%d {\"N%d\" : %d}\n", port, port, port, clock);
  writeToFile(logFileName, logBuf);
}

/*
  Construct the message for sending
*/
void constructMsg(msgType msgID, unsigned int electionID, unsigned long nodeId, struct msg* m){
  int i;
  m->msgID = msgID;
  m->electionID = electionID;
  for(i=0; i < MAX_NODES; i++){
    m->vectorClock[i].nodeId = myVectorClock[i].nodeId;
    m->vectorClock[i].time = myVectorClock[i].time;
  }
}

/*
  Handle a received message and send appropriate response message
*/
void handleMsg(unsigned long port, struct msg * recMsg, struct msg * newMsg, int sock, struct sockaddr_in from_addr, char * logFileName, struct groupMem * groupMems) {
  char recvMsg[512];              /* Buffer for the message */
  unsigned int fromAddrLen;        /* Length of incoming message */
  switch (recMsg->msgID) {
    case ELECT:
      updateMyVectorClock(port);
      constructMsg(ANSWER, recMsg->electionID, port, newMsg);
      sendMsg(sock, newMsg, from_addr);
      writeEventToLog(logFileName, newMsg, port, 1, ntohs(from_addr.sin_port));
      int i;
      lastReceived = ELECT;
      // If I send a election last time, don't send anymore.
      if(lastSent == ELECT) {
        break;
      }
      lastSent = 0;
      for (i = 0; i < numOfMembers; i++) {
        struct sockaddr_in to_addr;
        struct hostent *to_ip;
        
        if (groupMems[i].portno > port) {
          to_ip = gethostbyname(groupMems[i].hostname);
          if (to_ip == NULL) {
            fprintf(stderr,"ERROR, no such host\n");
            exit(0);
          }
          memset(&to_addr, 0, sizeof(to_addr));
          to_addr.sin_family = AF_INET;
          bcopy((char *)to_ip->h_addr, 
                (char *)&to_addr.sin_addr,
                to_ip->h_length);
          to_addr.sin_port = htons(groupMems[i].portno);

          updateMyVectorClock(port);
          constructMsg(ELECT, recMsg->electionID, port, newMsg);
          sendMsg(sock, newMsg, to_addr);
          writeEventToLog(logFileName, newMsg, port, 1, ntohs(to_addr.sin_port)); 
          lastSent = ELECT;
        }
      }
      if (lastSent == 0) {
        for (i = 0; i < numOfMembers; i++) {
          struct sockaddr_in mem_addr;
          struct hostent *mem_ip;

          if (groupMems[i].portno < port) {
            mem_ip = gethostbyname(groupMems[i].hostname);
            if (mem_ip == NULL) {
              fprintf(stderr,"ERROR, no such host\n");
              exit(0);
            }
            memset(&mem_addr, 0, sizeof(mem_addr));
            mem_addr.sin_family = AF_INET;
            bcopy((char *)mem_ip->h_addr, 
                  (char *)&mem_addr.sin_addr,
                  mem_ip->h_length);
            mem_addr.sin_port = htons(groupMems[i].portno);

            updateMyVectorClock(port);
            constructMsg(COORD, 0, port, newMsg);
            sendMsg(sock, newMsg, mem_addr);
            writeEventToLog(logFileName, newMsg, port, 1, ntohs(mem_addr.sin_port)); 
            lastSent = COORD;
          }
        }
      }
      break;
    case ANSWER:
      if (lastSent == ELECT) {        
        lastReceived = ANSWER;
        lastSent = 0;
      }
      break;
    case COORD:
      lastReceived = COORD;
      // set as COORD begin sending AYA
      // create and start thread to handle periodoic sends of AYA messages to coordinator
      if (threadPtr == NULL) {
        struct ThreadArgs *threadArgs; 
        threadArgs = (struct ThreadArgs *) malloc(sizeof(struct ThreadArgs));
        threadArgs->socket = sock;
        threadArgs->port = port;
        threadArgs->from_addr = from_addr;
        threadArgs->logFileName = logFileName;
      
        if (pthread_create(&threadID, NULL, ThreadMain, (void *) threadArgs) != 0) {
          perror("pthread_create() failed");
          threadPtr = NULL;
          exit(1);
        }
        threadPtr = &threadID;
      }
      break;
    case AYA:
      lastReceived = AYA;
      lastSent = IAA;
      updateMyVectorClock(port);
      constructMsg(IAA, recMsg->electionID, port, newMsg);
      sendMsg(sock, newMsg, from_addr);
      writeEventToLog(logFileName, newMsg, port, 1, ntohs(from_addr.sin_port));
      break;
    case IAA:
      break;
    default:
      break;
  }
}

/*
  Start routine of AYA thread
*/
void *ThreadMain(void *threadArgs) {
  int socket;
  unsigned long port;
  struct sockaddr_in from_addr;
  char * logFileName;

  if (pthread_detach(*threadPtr) != 0) {
    perror("pthread_detach failed");
    exit(1);
  } 
  socket = ((struct ThreadArgs *) threadArgs)->socket;
  port = ((struct ThreadArgs *) threadArgs)->port;
  from_addr = ((struct ThreadArgs *) threadArgs)->from_addr;
  logFileName = ((struct ThreadArgs *) threadArgs)->logFileName;
  free(threadArgs);

  while (1) {

    int i;
    int AYARate;
    for (i = 0; i < 10; i++) {
      int rn;
      rn = random();

    // scale to number between 0 and the 2*AYA time so that
    // the average value for the timeout is AYA time.

      AYARate = rn % (2*AYATime);
    }
    //printf("AYA send rate is: %d\n", AYARate);

    lastSent = AYA;
    struct msg newMsg;
    updateMyVectorClock(port);
    constructMsg(AYA, port, port, &newMsg);
    sendMsg(socket, &newMsg, from_addr);
    writeEventToLog(logFileName, &newMsg, port, 1, ntohs(from_addr.sin_port));
    sleep(AYARate);
  }
}

int main(int argc, char ** argv) {
  struct groupMem groupMems[MAX_NODES];

  unsigned long  port;
  char *         groupListFileName;
  char *         logFileName;
  unsigned long  myClock = 1;

  if (argc != 7) {
    usage(argv[0]);
    return -1;
  }

  char * end;
  int err = 0;

  port = strtoul(argv[1], &end, 10);
  if (argv[1] == end) {
    printf("Port conversion error\n");
    err++;
  }

  groupListFileName = argv[2];
  logFileName       = argv[3];

  timeoutValue      = strtoul(argv[4], &end, 10);
  if (argv[4] == end) {
    printf("Timeout value conversion error\n");
    err++;
  }

  AYATime  = strtoul(argv[5], &end, 10);
  if (argv[5] == end) {
    printf("AYATime conversion error\n");
    err++;
  }

  sendFailureProbability  = strtoul(argv[6], &end, 10);
  if (argv[5] == end) {
    printf("sendFailureProbability conversion error\n");
    err++;
  }

  printf("Port number:              %d\n", port);
  printf("Group list file name:     %s\n", groupListFileName);
  printf("Log file name:            %s\n", logFileName);
  printf("Timeout value:            %d\n", timeoutValue);  
  printf("AYATime:                  %d\n", AYATime);
  printf("Send failure probability: %d\n", sendFailureProbability);
  
  if (err) {
    printf("%d conversion error%sencountered, program exiting.\n",
      err, err>1? "s were ": " was ");
    return -1;
  }

  // create or truncate file to 0 before writing
  FILE * fp;
  fp = fopen(logFileName, "w");
  if (fp == NULL) {
    perror("fopen() failed");
    exit(1);
  }
  fclose(fp);

  // Read the Group List File, store the hostname/port pair in struct groupMems
  if (readGroupListFile(groupListFileName, groupMems) < 0) {
    return -1;
  }

  int nodePos;
  if ((nodePos = foundInGroup(port, groupMems)) == -1) {
    printf("Erro: Not a member of the group!\n");
    return -1;
  }

  int n;
  for (n = 0; n < MAX_NODES; n++) {
    myVectorClock[n].nodeId = 0;
    myVectorClock[n].time = 0;
  }

  // starting node
  counter = 1;
  writeStartToLog(logFileName, port, myClock);

  myVectorClock[nodePos].nodeId = port;
  myVectorClock[nodePos].time = myClock;

  srandom(time(NULL));

  sendUDPMsg(port, logFileName, groupMems);

  return 0;
}

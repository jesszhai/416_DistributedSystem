
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <errno.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netdb.h> 
#include "msg.h"
#include "helper.h"

// print usage
void usage(char * cmd, char * name, int type) {
  switch (type) {
    case BEGINTX:
    case JOINTX:
      printf("usage:  %s %s WORKER_HOST WORKER_PORT TX_MANAGER_HOST TX_MANAGER_PORT TID\n", cmd, name);
      break;
    case NEW_A:
    case NEW_B:
      printf("usage:  %s %s WORKER_HOST WORKER_PORT NEWVALUE \n", cmd, name);
      break;
    case NEW_IDSTR:
      printf("usage:  %s %s WORKER_HOST WORKER_PORT newIDSTR\n", cmd, name);
      break;
    case DELAY_RESPONSE:
      printf("usage:  %s %s WORKER_HOST WORKER_PORT DELAY\n", cmd, name);
      break;
    case CRASH:
    case COMMIT:
    case COMMIT_CRASH:
    case ABORT:
    case ABORT_CRASH:
    case VOTE_ABORT:
      printf("usage:  %s %s WORKER_HOST WORKER_PORT\n", cmd, name);
      break;
    default:
      printf("usage:  %s [CMDTYPE] [CMDARG] ...\n");
      printf("CMDTYPE:\n");
      printf("%8s%s\n", "", "begin");
      printf("%8s%s\n","", "join");
      printf("%8s%s\n", "", "newa");
      printf("%8s%s\n", "", "newb");
      printf("%8s%s\n", "", "newid");
      printf("%8s%s\n", "", "delay");
      printf("%8s%s\n", "", "crash");
      printf("%8s%s\n", "", "commit");
      printf("%8s%s\n", "", "commitcrash");
      printf("%8s%s\n", "", "abort");
      printf("%8s%s\n", "", "abortcrash");
      printf("%8s%s\n", "", "voteabort");
      break;
  }
  
}

/*
  convert command line args to integer
*/
uint32_t strToUInt(char * arg, char * end, char * argName, int * err) {
  uint32_t temp = strtoul(arg, &end, 10);
  if (arg == end) {
      printf("%s conversion error\n", argName);
      err++;
  }
  return temp;
}

int main(int argc, char ** argv) {
  
  char * cmdType;

  char * workerHost;
  unsigned long workerPort;

  msgType msg;

  // case: no arguments provided
  if (argc < 2) {
    usage(argv[0], "", 0);
    return -1;
  }

  char * end;
  int err = 0;

  // determine message type
  cmdType = argv[1];
  if (strcmp(cmdType, "begin") == 0) {
    msg.msgID = BEGINTX;
  } else if (strcmp(cmdType, "join") == 0) {
    msg.msgID = JOINTX;
  } else if (strcmp(cmdType, "newa") == 0) {
    msg.msgID = NEW_A;
  } else if (strcmp(cmdType, "newb") == 0) {
    msg.msgID = NEW_B;
  } else if (strcmp(cmdType, "newid") == 0) {
    msg.msgID = NEW_IDSTR;
  } else if (strcmp(cmdType, "crash") == 0) {
    msg.msgID = CRASH;
  } else if (strcmp(cmdType, "delay") == 0) {
    msg.msgID = DELAY_RESPONSE;
  } else if (strcmp(cmdType, "commit") == 0) {
    msg.msgID = COMMIT;
  } else if (strcmp(cmdType, "commitcrash") == 0) {
    msg.msgID = COMMIT_CRASH;
  } else if (strcmp(cmdType, "abort") == 0) {
    msg.msgID = ABORT;
  } else if (strcmp(cmdType, "abortcrash") == 0) {
    msg.msgID = ABORT_CRASH;
  } else if (strcmp(cmdType, "voteabort") == 0) {
    msg.msgID = VOTE_ABORT;
  } else {
    printf("usage: invalid CMDTYPE\n");
    return -1;
  }

  // case: no WORKER_HOST and WORKER_PORT
  if (argc < 4) {
    usage (argv[0], argv[1], msg.msgID);
    return -1;
  }

  // get worker host and port
  workerHost = argv[2];
  workerPort = strToUInt(argv[3], end, "WORKER_PORT", &err);

  // determine the remaining arguments depending on msg.ID
  // print usage if the arguments are incorrect
  switch (msg.msgID) {
    case BEGINTX:
    case JOINTX:
      if (argc < 7) {
        usage (argv[0], argv[1], msg.msgID);
        return -1;
      }
      if (strlen(argv[4]) > HOSTLEN - 1) {
        printf("TX_MANAGER_HOST must be of length %d\n", HOSTLEN - 1);
      } else {
        strcpy(msg.strData.hostName, argv[4]);
      }
      msg.port = strToUInt(argv[5], end, "TX_MANAGER_PORT", &err);
      msg.tid = strToUInt(argv[6], end, "TID", &err);
      break;
    case NEW_A:
    case NEW_B:
      if (argc < 5) {
        usage (argv[0], argv[1], msg.msgID);
        return -1;
      }
      msg.newValue = strToUInt(argv[4], end, "NEWVALUE", &err);
      break;
    case NEW_IDSTR:
      if (argc < 5) {
        usage (argv[0], argv[1], msg.msgID);
        return -1;
      }
      if (strlen(argv[4]) > IDLEN -1) {
        printf("NEW_IDSTR must be of length %d\n", IDLEN - 1);
        return -1;
      } else {
        strcpy(msg.strData.newID, argv[4]);
      }
      break;
    case DELAY_RESPONSE:
      if (argc < 5) {
        usage (argv[0], argv[1], msg.msgID);
        return -1;
      }
      msg.delay = strToUInt(argv[4], end, "DELAY", &err);
      break;
    case CRASH:
    case COMMIT:
    case COMMIT_CRASH:
    case ABORT:
    case ABORT_CRASH:
    case VOTE_ABORT:
    default:
      break;
  }
  

  if (err) {
    printf("%d conversion error%sencountered, program exiting.\n",
      err, err>1? "s were ": " was ");
    return -1;
  }

  // create and send message to tworker specified in WORKER_HOST and WORKER_PORT
  char buffer[sizeof(msgType)];
  struct sockaddr_in toAddr;
  struct hostent *toIP;
  int toAddrLen;

  int sock;
  sock = createCmdSocket();

  toIP = gethostbyname(workerHost);
  if (toIP == NULL) {
    fprintf(stderr,"ERROR, no such host\n");
    exit(0);
  }
  memset(&toAddr, 0, sizeof(toAddr));
  toAddr.sin_family = AF_INET;
  bcopy((char *)toIP->h_addr, 
        (char *)&toAddr.sin_addr,
        toIP->h_length);
  toAddr.sin_port = htons(workerPort);

  serializeMsg(buffer, msg);
  toAddrLen = sizeof(toAddr);
  if (sendto(sock, buffer, sizeof(msgType), 0, (struct sockaddr *) &toAddr, toAddrLen) != sizeof(msgType)) {
    perror("sendto() sent a different number of bytes than expected");
    return -1;
  }
  _exit(23);

  
}

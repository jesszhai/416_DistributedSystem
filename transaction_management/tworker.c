
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <stdio.h>
#include <netinet/in.h>
#include <netinet/udp.h>
#include <errno.h>
#include <sys/mman.h>
#include <string.h>
#include <netdb.h> 
#include <sys/poll.h>

#include "msg.h"
#include "tworker.h"
#include "helper.h"

char           logFileName[128];
char           ShivizFileName[128];
struct clock   myVectorClock[MAX_NODES]; 
CrashInfo      *crashInfo;
unsigned int   nodeId;
uint32_t       curTid;
int            isTXRun = 0;
int            delay = 0;
int            isCrash = 0;
int            vote = 0;
int            firstTimeWaiting = 0;

/*
  Find the state of the worker before crash from the transaction log
  Used to recover from crash
*/
int findStateFromRecord() {
  struct stat fstatus;
  int fd;

  fd = open(logFileName, O_RDONLY);
  if (fstat(fd, &fstatus) < 0 ) {
    perror("Filestat failed");
    exit(1);
  }
  close(fd);

  int numRec = (fstatus.st_size - sizeof(CrashInfo))/sizeof(Record);
  Record records[numRec];
  Record * rec = (Record *) (crashInfo + 1);
  int i;
  if (numRec > 0) {
    Record r = *(rec + (numRec - 1));
    curTid = r.tid;
    return r.msgID;
  }
  return 0;
}

/*
  Determine if the worker aborted its current transaction locally
  Return 1 if local abort
  Return 0 if there is no local abort
*/
int isLocalAbort(ObjectData * objData){
  struct stat fstatus;
  int fd;

  fd = open(logFileName, O_RDONLY);
  if (fstat(fd, &fstatus) < 0 ) {
    perror("Filestat failed");
    exit(1);
  }
  close(fd);

  int numRec = (fstatus.st_size - sizeof(CrashInfo))/sizeof(Record);
  Record records[numRec];
  Record * rec = (Record *) (crashInfo + 1);
  int i;
  for (i = 0; i < numRec; i++) {
    Record * r = rec + i;
    records[i] = *r;
  }

  int j;
  for (j = numRec - 1; j >= 0; j--) {
    Record r = records[j];
    if (r.tid == curTid) {
      if(r.msgID == ABORT){
        return 1;
      }
    }
  }
  return 0;
}

/*
  Read old values for ObjectData's A, B and IDstring values from log
  Used to rollback values in the abort case
*/
int readValFromRecord(ObjectData * objData) {
  struct stat fstatus;
  int fd;

  fd = open(logFileName, O_RDONLY);
  if (fstat(fd, &fstatus) < 0 ) {
    perror("Filestat failed");
    exit(1);
  }
  close(fd);

  int numRec = (fstatus.st_size - sizeof(CrashInfo))/sizeof(Record);
  Record records[numRec];
  Record * rec = (Record *) (crashInfo + 1);
  int i;
  for (i = 0; i < numRec; i++) {
    Record * r = rec + i;
    records[i] = *r;
  }
  int j;
  int oldA = objData->A;
  int oldB = objData->B;
  char oldID[IDLEN];
  strcpy(oldID, objData->IDstring);
  for (j = numRec - 1; j >= 0; j--) {
    Record r = records[j];
    if (r.tid == curTid) {
      switch (r.msgID) {
        case NEW_A:
          oldA = r.oldValue;
          break;
        case NEW_B:
          oldB = r.oldValue;
          break;
        case NEW_IDSTR:
          strcpy(oldID, r.oldID);
          break;
        case JOINTX:
          objData->A = oldA;
          objData->B = oldB;
          strcpy(objData->IDstring, oldID);
          int k;
          for (k = 0; k < MAX_NODES; k++) {
            objData->vectorClock[k] = r.vectorClock[k];
          }
          objData->lastUpdateTime = r.lastUpdateTime;
          msync(objData, sizeof(ObjectData), MS_SYNC);
          break;
        default:
          break;
      }
    } else {
      break;
    }
  }
  return 1;
}

/*
  Write a record into the transaction log
*/
int writeRecordToLog(Record rec) {
  FILE * fp;
  fp = fopen(logFileName, "ab");
  if (fp == NULL) {
    perror("fopen() failed");
    exit(1);
  }
  if (fwrite(&rec, sizeof(Record), 1, fp) == EOF) {
    perror("fputs() failed");
    exit(1);
  }
  fclose(fp);
  return 1;
}

/*
  Save the port that the worker uses to communicate with the manager 
  into the beginning of the log
*/
int savePort(int sock) {
  if (crashInfo == 0) {
    perror("Object data could not be mapped in");
    return -1;
  }

  struct sockaddr_in tmpAddr;
  unsigned long tmport;
  unsigned int len;
  len = sizeof(tmpAddr);

  getsockname(sock, (struct sockaddr *) &tmpAddr, &len);
  tmport = ntohs(tmpAddr.sin_port);
  printf("Generated port is: %d\n", tmport);

  crashInfo->sys_port = tmport;
  msync(crashInfo, sizeof(CrashInfo), MS_SYNC);
  
  return 0;
}

/*
  Restore the worker's own vector clock values after a crash
  by looking at the vector clock recorded in transaction log
*/
int restoreVectorTime(){
  if (crashInfo == 0) {
    perror("Object data could not be mapped in");
    return -1;
  }

  int i;
  for (i=0; i<MAX_NODES; i++) {
    myVectorClock[i].nodeId = crashInfo->vectorClock[i].nodeId;
    myVectorClock[i].time = crashInfo->vectorClock[i].time;
  }
  
  msync(crashInfo, sizeof(CrashInfo), MS_SYNC);

  return 0;
}

/*
  Save the update vector time clock into the transaction log
*/
int saveVectorTime(){
  if (crashInfo == 0) {
    perror("Object data could not be mapped in");
    return -1;
  }

  int i;
  for(i=0; i<MAX_NODES; i++){
    crashInfo->vectorClock[i].nodeId = myVectorClock[i].nodeId;
    crashInfo->vectorClock[i].time = myVectorClock[i].time;
  }
  msync(crashInfo, sizeof(CrashInfo), MS_SYNC);

  return 0;
}

/*
  Update the vector clock by merging it with the vector clock
  received from the message sender
*/
void mergeVectorClock(txMessage m){
  int i, j;
  for (i = 0; i < MAX_NODES; i++) {
    if (m.vectorClock[i].nodeId == 0) {
      break;
    }
    for (j = 0; j < MAX_NODES; j++) {
      if((m.vectorClock[i].nodeId == myVectorClock[j].nodeId)){
        if(m.vectorClock[i].time > myVectorClock[j].time){
          myVectorClock[j].time = m.vectorClock[i].time;
        }
        break;
      }
      else if (myVectorClock[j].nodeId == 0) {
        myVectorClock[j].nodeId = m.vectorClock[i].nodeId;
        myVectorClock[j].time = m.vectorClock[i].time;
        break;
      }
    }
  }
}

/*
  Get the worker port used to communicate with manager that is
  stored at the beginning of the transaction log file
  Used when recovering from a worker crash
*/
unsigned long getPortfromLogFile(){
  if (crashInfo == 0) {
    perror("Object data could not be mapped in");
    return -1;
  }

  printf("Port Num: %d\n", crashInfo->sys_port);

  return crashInfo->sys_port;
}

/*
  Construct worker local events to write to ShiViz log
*/
void writeLocalEventToLog(char * fileName, msgType m, unsigned long port){
  char logBuf[500];
  char vectorTimeBuf[500];
  char portBuf[10];
  switch (m.msgID) {
    case NEW_A:
      sprintf(logBuf, "Update A value to %d" ,  m.newValue);
      break;
    case NEW_B:
      sprintf(logBuf, "Update B value to %d" , m.newValue);
      break;
    case NEW_IDSTR:
      sprintf(logBuf, "Update ID String to %s ", m.strData.newID);
      break;
    case ABORT:
      sprintf(logBuf, "Locally ABORT");
      break;
    case ABORT_CRASH:
      sprintf(logBuf, "Locally ABORT_CRASH");
      break;
    case CRASH:
      sprintf(logBuf, "Locally CRASH");
      break;
    case RESTARTED:
      sprintf(logBuf, "Worker RESTARTED");
      break;
    default:
      break;
  }
  strcat(logBuf, "\n");
  writeVectorTime(vectorTimeBuf, &myVectorClock[0], port);
  strcat(logBuf, vectorTimeBuf);
  writeToFile(fileName, logBuf);
}

/*
  Construct description of event to be logged
*/
void writeEventToLog(char * fileName, txMessage m, unsigned long port, int isSend, unsigned long toFromNodeId) {
  char logBuf[500];
  char vectorTimeBuf[500];
  char portBuf[10];
  switch (m.msgID) {
    case BEGIN_SUCCESS:
    case BEGINTX:
      sprintf(logBuf, isSend ? "Send BEGIN Transaction %lu to " : "Receive BEGIN_SUCCESS %lu from ", m.tid);
      break;
    case JOIN_SUCCESS:
    case JOINTX:
      sprintf(logBuf, isSend ? "Send JOIN Transaction %lu to " : "Receive JOIN_SUCCESS %lu from ", m.tid);
      break;
    case JOIN_FAIL:
      sprintf(logBuf, isSend ? "SEND JOIN_FAIL Transaction %lu to " : "Receive JOIN_FAIL Transaction %lu from ", m.tid);
      break;
    case BEGIN_FAIL:
      sprintf(logBuf, isSend ? "SEND BEGIN_FAIL Transaction %lu to " : "Receive BEGIN_FAIL Transaction %lu from ", m.tid);
      break;
    case PREPARE_COMMIT:
    case COMMIT:
      sprintf(logBuf, isSend ? "SEND COMMIT Transaction %lu to " : "Receive PREPARE_COMMIT Transaction %lu from ", m.tid);
      break;
    case COMMIT_CRASH:
      sprintf(logBuf, isSend ? "SEND COMMIT_CRASH Transaction %lu to " : "Receive PREPARE_COMMIT Transaction %lu from ", m.tid);
      break;
    case DECIDE_COMMIT:
    case VOTE_COMMIT:
      sprintf(logBuf, isSend ? "SEND VOTE_COMMIT Transaction %lu to " : "Receive DECIDE_COMMIT Transaction %lu from ", m.tid);
      break;
    case DECIDE_ABORT:
    case VOTE_ABORT:
      sprintf(logBuf, isSend ? "SEND VOTE_ABORT Transaction %lu to " : "Receive DECIDE_ABORT Transaction %lu from ", m.tid);
      break;
    case POLL_DECISION:
      sprintf(logBuf, isSend ? "SEND POLL_DECISION Transaction %lu to " : "Receive POLL_DECISION Transaction %lu from ", m.tid);
      break;
    default:
      break;
  }
  sprintf(portBuf, "N%lu", toFromNodeId);
  strcat(logBuf, portBuf);
  strcat(logBuf, "\n");
  writeVectorTime(vectorTimeBuf, &myVectorClock[0], port);
  strcat(logBuf, vectorTimeBuf);
  writeToFile(fileName, logBuf);
}

/* 
  Construct a new message to send to manager from a message received
  from the cmd program
*/
void constructMessage(msgType m, txMessage* newMsg, int msgKind){
  int i;
  if(msgKind == 0){
    newMsg->msgID = m.msgID;
  } else {
    newMsg->msgID = msgKind;
  }
  newMsg->tid = m.tid;
  newMsg->nodeId = nodeId;
  for(i=0; i < MAX_NODES; i++){
    newMsg->vectorClock[i].nodeId = myVectorClock[i].nodeId;
    newMsg->vectorClock[i].time = myVectorClock[i].time;
  }
}

/*
  Construct a new message to send to manager from a message received
  from manager
*/
void constructTxMessage(txMessage m, txMessage* newMsg, int msgKind){
  int i;
  newMsg->msgID = msgKind;
  newMsg->tid = m.tid;
  newMsg->nodeId = m.nodeId;
  newMsg->isCrash = isCrash;
  for(i=0; i < MAX_NODES; i++){
    newMsg->vectorClock[i].nodeId = myVectorClock[i].nodeId;
    newMsg->vectorClock[i].time = myVectorClock[i].time;
  }
}

/*
  Send a message to the transaction manager at the specified address
  If the worker previously received a DELAY_RESPONSE from cmd program,
  check the delay value here and delay/crash/perform actions as
  specified in assignment description
*/
int sendMsg(int sock, txMessage m, struct sockaddr_in from_addr) {
  msgType msg;
  unsigned int fromAddrLen = sizeof(from_addr);
  char msgBuf[sizeof(txMessage)];
  serializeWorkerMsg(msgBuf, m);
  if (delay == -1000) {   
    msg.msgID = CRASH;
    updateMyVectorClock(m.nodeId, &myVectorClock[0]);
    saveVectorTime();
    writeLocalEventToLog(ShivizFileName, msg, nodeId);       
    _exit(0);
  } else if (delay < 0) {
    sleep(abs(delay));
  } else {
    sleep(delay);
  }
  if (sendto(sock, msgBuf, sizeof(txMessage), 0, (struct sockaddr *) &from_addr, fromAddrLen) != sizeof(txMessage)){
    perror("sendto() sent a different number of bytes than expected");
    return -1;
  }
  if (delay < 0) {
    msg.msgID = CRASH;
    updateMyVectorClock(m.nodeId, &myVectorClock[0]);
    saveVectorTime();
    writeLocalEventToLog(ShivizFileName, msg, nodeId);
    _exit(0);
  }
}

/*
  Polls for response from transaction manager when worker is in uncertain state
*/
int waitingForCoordResponse (int managerSock, struct sockaddr_in managerAddr, ObjectData * objData){
  char recvBuf[512];
  unsigned int manAddrLen; 
  int recvMsgSize;
  txMessage newMsg;
  int rv;

  Record rec;

  struct pollfd ufds[1];
  ufds[0].fd = managerSock;
  ufds[0].events = POLLIN;

  // if waiting for manager response for the first time, wait for 30s
  if (firstTimeWaiting == 1) {
    rv = poll(ufds, 1, 30000);
  } else {
    // if we have crashed and need to poll manager for decision, we want to poll immediately 
    rv = poll(ufds, 1, 0);
  } 
 
  for (;;) {
    if (rv == -1) {
      perror("poll"); // error occurred in poll()
    } else if (rv == 0) {
      txMessage ourMsg;
      firstTimeWaiting = 0;
      printf("Timeout occurred!  No data after 10 seconds.\n");
      updateMyVectorClock(nodeId, &myVectorClock[0]);
      saveVectorTime();
      ourMsg.nodeId = nodeId;
      ourMsg.tid = curTid;
      constructTxMessage(ourMsg, &newMsg, POLL_DECISION);
      writeEventToLog(ShivizFileName, newMsg, nodeId, 1, ntohs(managerAddr.sin_port));
      sendMsg(managerSock, newMsg, managerAddr);
    } else {
      manAddrLen = sizeof(managerAddr);
      if ((recvMsgSize = recvfrom(managerSock, recvBuf, 512, 0, (struct sockaddr *) &managerAddr, &manAddrLen)) > 0) {
        txMessage msg = deserializeWorkerMsg(recvBuf);
        printWorkerMsg(msg);
        mergeVectorClock(msg);
        updateMyVectorClock(nodeId, &myVectorClock[0]);
        saveVectorTime();
        writeEventToLog(ShivizFileName, msg, nodeId, 0, ntohs(managerAddr.sin_port));
        switch (msg.msgID) {
          case DECIDE_COMMIT:
            rec.msgID = msg.msgID;
            rec.tid = msg.tid;
            writeRecordToLog(rec);
            isTXRun = 0;
            break;
          case DECIDE_ABORT:
            rec.msgID = msg.msgID;
            rec.tid = msg.tid;
            readValFromRecord(objData);
            writeRecordToLog(rec);
            isTXRun = 0;
            break;
          default:
            break;
        }
        return 0;
      }
    }
    // subsequent polls should timeout in 10s
    rv = poll(ufds, 1, 10000);
  }
}

/*
  Handle messages received from the transaction manager
*/
int handleManagerMsg(int managerSock, struct sockaddr_in managerAddr, ObjectData * objData){

  char recvBuf[512];
  unsigned int manAddrLen; 
  int recvMsgSize;
  txMessage newMsg;
  Record rec;

  manAddrLen = sizeof(managerAddr);
  if ((recvMsgSize = recvfrom(managerSock, recvBuf, 512, 0, (struct sockaddr *) &managerAddr, &manAddrLen)) > 0) {
    txMessage msg = deserializeWorkerMsg(recvBuf);
    printWorkerMsg(msg);
    mergeVectorClock(msg);
    updateMyVectorClock(nodeId, &myVectorClock[0]);
    saveVectorTime();
    writeEventToLog(ShivizFileName, msg, nodeId, 0, ntohs(managerAddr.sin_port));
    switch (msg.msgID) {
      case JOIN_FAIL:
      case JOIN_SUCCESS:
        if(msg.msgID == JOIN_SUCCESS){
          curTid = msg.tid;
          isTXRun = 1;
          crashInfo->managerAddr = managerAddr;
        }
        rec.msgID = JOINTX;
        rec.tid = msg.tid;
        int i;
  
        for (i = 0; i < MAX_NODES; i++) {
          rec.vectorClock[i] = objData->vectorClock[i];
        } 
        rec.lastUpdateTime = objData->lastUpdateTime; 
        writeRecordToLog(rec);
        break;
      case BEGIN_FAIL:
        break;
      case BEGIN_SUCCESS:
        rec.msgID = BEGINTX;
        rec.tid = msg.tid;
        break;
      case DECIDE_COMMIT:
        rec.msgID = msg.msgID;
        rec.tid = msg.tid;
        writeRecordToLog(rec);
        isTXRun = 0;
        break;
      case PREPARE_COMMIT:
        rec.msgID = msg.msgID;
        rec.tid = msg.tid;
        writeRecordToLog(rec);
        // by Default, vote commit
        updateMyVectorClock(nodeId, &myVectorClock[0]);
        saveVectorTime();
        if(isLocalAbort(objData) == 1 || vote == VOTE_ABORT){
          constructTxMessage(msg, &newMsg, VOTE_ABORT);
          vote = 0;
          rec.msgID = VOTE_ABORT;
        } else {
          constructTxMessage(msg, &newMsg, VOTE_COMMIT);
          rec.msgID = VOTE_COMMIT;
        }
        writeEventToLog(ShivizFileName, newMsg, nodeId, 1, ntohs(managerAddr.sin_port));
        sendMsg(managerSock, newMsg, managerAddr);
        rec.msgID = VOTE_COMMIT;
        rec.tid = msg.tid;
        writeRecordToLog(rec);
        // after we vote.. we are in uncertain state, block and wait
        firstTimeWaiting = 1;
        waitingForCoordResponse(managerSock, managerAddr, objData);
        break;
      case DECIDE_ABORT:
        rec.msgID = msg.msgID;
        rec.tid = msg.tid;
        readValFromRecord(objData);
        writeRecordToLog(rec);
        isTXRun = 0;
        break;
      default:
        break;
    }
  }
}

/*
  Handle messages received from the cmd program
*/
int handleCmdMsg(int cmdSock, int managerSock, ObjectData * objData) {
  struct sockaddr_in cmdAddr;    /* Receiver address */
  unsigned int cmdAddrLen;        /* Length of incoming message */
  char cmdHost[10];
  unsigned long cmdPortNo;
  char recvBuf[512];
  int recvMsgSize;
  int rv; 

  uint32_t tid;
  uint32_t manager_port;
  struct hostent *toIP;  

  struct sockaddr_in toAddr;
  int toAddrLen;
  txMessage newMsg;
  int i;

  Record rec;

  struct pollfd ufds[2];
  ufds[0].fd = cmdSock;
  ufds[0].events = POLLIN;

  ufds[1].fd = managerSock;
  ufds[1].events = POLLIN;

for(;;) {
  rv = poll(ufds, 2, 10000);

  if (rv == -1) {
    perror("poll"); // error occurred in poll()
  } else if (rv == 0) {
    printf("Timeout occurred!  No data after 10 seconds.\n");
  } else {
    // check for events on cmdSock:
    if (ufds[0].revents & POLLIN) {
      recvMsgSize = recvfrom(cmdSock, recvBuf, 512, 0, (struct sockaddr *) &cmdAddr, &cmdAddrLen);
      cmdPortNo = ntohs(cmdAddr.sin_port);
      msgType msg = deserializeMsg(recvBuf);
      int myTime;
      // send the cmd to manager
      switch (msg.msgID) {
        case BEGINTX:
        case JOINTX:
          toIP = gethostbyname(msg.strData.hostName);
          manager_port = msg.port;
         
          break;
        case NEW_A:
          if (isTXRun) {
            rec.msgID = msg.msgID;
            rec.tid = curTid;
            rec.oldValue = objData->A;
            writeRecordToLog(rec);
          }
          objData->A = msg.newValue;
          myTime = updateMyVectorClock(nodeId, &myVectorClock[0]);
          saveVectorTime();
          writeLocalEventToLog(ShivizFileName, msg, nodeId);
          gettimeofday(&objData->lastUpdateTime, NULL);
          objData->vectorClock[myTime].time = myVectorClock[myTime].time;
          msync(objData, sizeof(ObjectData), MS_SYNC);
          break;
        case NEW_B:
          if (isTXRun) { 
            rec.msgID = msg.msgID;
            rec.tid = curTid;
            rec.oldValue = objData->B;
            writeRecordToLog(rec);
          }
          objData->B = msg.newValue;
          myTime = updateMyVectorClock(nodeId, &myVectorClock[0]);
          saveVectorTime();
          writeLocalEventToLog(ShivizFileName, msg, nodeId);
          gettimeofday(&objData->lastUpdateTime, NULL);
          objData->vectorClock[myTime].time = myVectorClock[myTime].time;
          msync(objData, sizeof(ObjectData), MS_SYNC);
          break;
        case NEW_IDSTR:
          if (isTXRun) {
            rec.msgID = msg.msgID;
            rec.tid = curTid;
            strcpy(rec.oldID, objData->IDstring);
            writeRecordToLog(rec);
          }
          strcpy(objData->IDstring, msg.strData.newID);
          myTime = updateMyVectorClock(nodeId, &myVectorClock[0]);
          saveVectorTime();
          writeLocalEventToLog(ShivizFileName, msg, nodeId);
          gettimeofday(&objData->lastUpdateTime, NULL);
          objData->IDstring[strlen(objData->IDstring)] = '\0';
          msync(objData, sizeof(ObjectData), MS_SYNC);
          break;
        case DELAY_RESPONSE:
          delay = msg.delay;
          break;
        case CRASH:
          updateMyVectorClock(nodeId, &myVectorClock[0]);
          saveVectorTime();
          writeLocalEventToLog(ShivizFileName, msg, nodeId);
          _exit(0);
          break;
        case COMMIT_CRASH:
          isCrash = 1;
          rec.msgID = msg.msgID;
          msg.tid = curTid;
          rec.tid = msg.tid;
          writeRecordToLog(rec);
          break;
        case COMMIT:
          isCrash = 0;
          rec.msgID = msg.msgID;
          msg.tid = curTid;
          rec.tid = msg.tid;
          writeRecordToLog(rec);
          break;
        case ABORT_CRASH:
          isCrash = 1;
          readValFromRecord(objData);
          updateMyVectorClock(nodeId, &myVectorClock[0]);
          writeLocalEventToLog(ShivizFileName, msg, nodeId);
          saveVectorTime();
          rec.msgID = ABORT;
          msg.tid = curTid;
          rec.tid = msg.tid;
          writeRecordToLog(rec);
          break;
        case ABORT:
          isCrash = 0;
          readValFromRecord(objData);
          updateMyVectorClock(nodeId, &myVectorClock[0]);
          writeLocalEventToLog(ShivizFileName, msg, nodeId);
          saveVectorTime();
          rec.msgID = msg.msgID;
          msg.tid = curTid;
          rec.tid = msg.tid;
          writeRecordToLog(rec);
          break;
        case VOTE_ABORT:
          vote = VOTE_ABORT;
          break;
        default:
          break;
      }

      switch (msg.msgID) {
        case BEGINTX:
        case JOINTX:
          memset(&toAddr, 0, sizeof(toAddr));
          toAddr.sin_family = AF_INET;
          memcpy((char *)toIP->h_addr, 
                (char *)&toAddr.sin_addr,
                toIP->h_length);
          toAddr.sin_port = htons(manager_port);

          updateMyVectorClock(nodeId, &myVectorClock[0]);
          saveVectorTime();
          constructMessage(msg, &newMsg, 0);
          writeEventToLog(ShivizFileName, newMsg, nodeId, 1, ntohs(toAddr.sin_port));
          sendMsg(managerSock, newMsg, toAddr);
          break;
        case COMMIT:
        case COMMIT_CRASH:
          updateMyVectorClock(nodeId, &myVectorClock[0]);
          saveVectorTime();
          msg.msgID = COMMIT;
          constructMessage(msg, &newMsg, 0);
          writeEventToLog(ShivizFileName, newMsg, nodeId, 1, ntohs(toAddr.sin_port));
          sendMsg(managerSock, newMsg, toAddr);
          break;
        default:
          break;
      }
    }

    if (ufds[1].revents & POLLIN) {
      handleManagerMsg(managerSock, toAddr, objData);
    }
  }
}
  return 0;
}

void usage(char * cmd) {
  printf("usage: %s  portNum\n",
   cmd);
}

/*
  Main function
  Initailize state (vector clocks and ObjectData)
  On restart from crash, restore to previous state before crash
*/
int main(int argc, char ** argv) {

  // This is some sample code feel free to delete it
  
  int            fileExist;
  unsigned long  port;
  unsigned long  sys_port = 0;
  char           dataObjectFileName[128];
  int            logfileFD;
  int            dataObjectFD;
  int            vectorLogFD;
  ObjectData     *objData;
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
    err++;
  } else {
    /* got the port number create a logfile name */
    snprintf(logFileName, sizeof(logFileName), "WorkerLog_%d.log", port);

    // check if logfile exist
    if ( access( logFileName, F_OK ) != -1 ) {
      fileExist = 1;
    }

    logfileFD = open(logFileName, O_RDWR | O_CREAT | O_SYNC, S_IRUSR | S_IWUSR );
    if (logfileFD < 0 ) {
      char msg[256];
      snprintf(msg, sizeof(msg), "Opening %s failed", logFileName);
      perror(msg);
    }

    if (fstat(logfileFD, &fstatus) < 0 ) {
      perror("Filestat failed");
      return -2;
    }

    if (fstatus.st_size < sizeof(CrashInfo)) {
     CrashInfo  space;
     retVal = write(logfileFD, &space, sizeof(CrashInfo));
     if (retVal != sizeof(CrashInfo)) {
      printf("Some sort of writing error\n");
      return -3;
     }
    }

    crashInfo = mmap(NULL, 512, PROT_WRITE|PROT_READ, MAP_SHARED, logfileFD, 0);
  }
  
    /* Open/create the data object file */

    snprintf(dataObjectFileName, sizeof(dataObjectFileName), 
       "WorkerData_%d.data", port);

    dataObjectFD = open(dataObjectFileName, 
         O_RDWR | O_CREAT | O_SYNC, S_IRUSR | S_IWUSR );
    if (dataObjectFD < 0 ) {
      char msg[256];
      snprintf(msg, sizeof(msg), "Opening %s failed", dataObjectFileName);
      perror(msg);
      err++;
    } else {
      // Open/create the ShiViz log file - Note that we are assuming that if the 
      // log file exists that this is the restart of a worker and hence as part of the 
      // recover process will get its old vector clock value and pick up from there.
      // Consquently we will always be appending to a log file. 
      snprintf(dataObjectFileName, sizeof(dataObjectFileName), 
         "ShiViz_%d.dat", port);
      strncpy(ShivizFileName, dataObjectFileName, 128);
      vectorLogFD = open(dataObjectFileName, 
       O_WRONLY | O_CREAT | O_APPEND | O_SYNC, S_IRUSR | S_IWUSR );
      if (vectorLogFD < 0 ) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Opening %s failed", dataObjectFileName);
        perror(msg);
        err++;
      }
    }

  if (err) {
    printf("%d transaction manager initialization error%sencountered, program exiting.\n",
     err, err>1? "s were ": " was ");
    return -1;
  }

  int cmdSock;
  // create sock listening for command from cmd program
  cmdSock = createSocket(port);
  nodeId = port;

  printf("cmd Socket created\n");

  // create sock for communicating with trans manager
  int managerSock;
  if (fileExist) {
    // if log file exist, that means we restarted with the same port
    sys_port = getPortfromLogFile();
    if (sys_port < 0) {
      return -1; 
    }
  }

  managerSock = createSocket(sys_port);

  if (cmdSock < 0 || managerSock < 0) {
    return -1;
  }

  // save system generated port in log
  if (sys_port == 0) {
    savePort(managerSock);
  }
  
  printf("Starting up transaction worker on %d\n", port);
  printf("Port number:                      %d\n", port);
  printf("Log file name:                    %s\n", logFileName);


  if (fstat(dataObjectFD, &fstatus) < 0 ) {
    perror("Filestat failed");
    return -2;
  }

  if (fstatus.st_size < sizeof(ObjectData)) {
    /* File hasn's been mapped in before 
       so we need to make sure there is enough
       space used in the file to hold 
       the data.
    */
    ObjectData  space;
    retVal = write(dataObjectFD, &space, sizeof(ObjectData));
    if (retVal != sizeof(ObjectData)) {
      printf("Some sort of writing error\n");
      return -3;
    }
  }

  objData = mmap(NULL, 512, PROT_WRITE|PROT_READ, MAP_SHARED, dataObjectFD, 0);
  
  if (objData == 0) {
    perror("Object data could not be mapped in");
    return -1;
  }
  
  // if we are not recovering, intialize the ObjectData and vector clocks
  if (!fileExist) {
    gettimeofday(&objData->lastUpdateTime, NULL);
    objData->vectorClock[0].nodeId = port;
    objData->vectorClock[0].time = 1;

    msync(objData, sizeof(ObjectData), MS_SYNC);

    int i;
    for (i = 1; i < MAX_NODES; i++) {
      objData->vectorClock[i].nodeId = 0;
      objData->vectorClock[i].time = 0; 
    }

    for (i = 0; i < MAX_NODES; i++) {
      myVectorClock[i].nodeId = 0;
      myVectorClock[i].time = 0; 
    }
    myVectorClock[0].nodeId = port;
    myVectorClock[0].time = 1;

    writeStartToLog(ShivizFileName, port, 1);

    if (saveVectorTime() < 0) {
      return -1;
    }
  } else {
    // restoring to state before crash
    if (restoreVectorTime() < 0) {
      return -1;
    }
    msgType eventMsg;
    eventMsg.msgID = RESTARTED;
    updateMyVectorClock(nodeId, &myVectorClock[0]);
    saveVectorTime();
    writeLocalEventToLog(ShivizFileName, eventMsg, nodeId);

    int state = findStateFromRecord();

    if (state != ABORT && state != DECIDE_ABORT && state != PREPARE_COMMIT && state != VOTE_COMMIT && state != DECIDE_COMMIT) {
      msgType msg;
      msg.msgID = ABORT;
      msg.tid = curTid;
      readValFromRecord(objData);
      updateMyVectorClock(nodeId, &myVectorClock[0]);
      writeLocalEventToLog(ShivizFileName, msg, nodeId);
      saveVectorTime();
      Record rec;
      rec.msgID = ABORT;
      rec.tid = msg.tid;
      writeRecordToLog(rec);
      isTXRun = 0;
    } else {
      waitingForCoordResponse(managerSock, crashInfo->managerAddr, objData);
    }
  }

  msync(objData, sizeof(ObjectData), MS_SYNC);
  
  if(handleCmdMsg(cmdSock, managerSock, objData) < 0)
    return -1;

  retVal = munmap(objData, sizeof(ObjectData));
  if (retVal < 0) {
    perror("Unmap failed");
  }

  // unmap crashInfo
  retVal = munmap(crashInfo, sizeof(CrashInfo));
  if (retVal < 0) {
    perror("Unmap failed");
  }
  

  return 0;

}

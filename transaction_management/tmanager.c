
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
#include <stdlib.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h> 
#include <sys/poll.h>


#include "msg.h"
#include "helper.h"
#include "tworker.h"
#include "tmanager.h"

char           logFileName[128];
char           ShivizFileName[128];
struct clock   myVectorClock[MAX_NODES]; 
unsigned long  port;
uint32_t       abortTid;

// state of transactions on manager recovery
struct state {
  uint32_t tid;
  int      isCommit;
};

struct state state[MAX_TRANS];

struct CoordCrashInfo   *crashInfo;

/*
  Determine the last state of each transaction that was managed by the transaction manager
  before crashing by looking at the manager's transaction log
*/
void findStateFromRecord() {
  struct stat fstatus;
  int fd;

  fd = open(logFileName, O_RDONLY);
  if (fstat(fd, &fstatus) < 0 ) {
    perror("Filestat failed");
    exit(1);
  }
  close(fd);

  int numRec = (fstatus.st_size - sizeof(struct CoordCrashInfo))/sizeof(CoordRecord);
  CoordRecord records[numRec];
  CoordRecord * rec = (CoordRecord *) (crashInfo + 1);
  int i;
  for (i = 0; i < numRec; i++) {
    CoordRecord * r = rec + i;
    records[i] = *r;
  }

  for (i = 0; i < MAX_TRANS; i++) {
    state[i].tid = crashInfo->workerInfo[i].tid;
    state[i].isCommit = 0;
  }
  
  int j;
  uint32_t curTid = 0;
  int status[MAX_TRANS];
  for (j = numRec - 1; j >= 0; j--) {
    CoordRecord r = records[j];
    if (r.msgID == DECIDE_COMMIT) {
      int k;
      for (k = 0; k < MAX_TRANS; k++) {
        if (r.tid == state[k].tid) {
          state[k].isCommit = 1;
        }
      }
    }
  }
}

/*
  Check if the manager is still waiting for votes (i.e. whether it has made a decision)
*/
int waitingForVotes() {
  struct stat fstatus;
  int fd;

  fd = open(logFileName, O_RDONLY);
  if (fstat(fd, &fstatus) < 0 ) {
    perror("Filestat failed");
    exit(1);
  }
  close(fd);

  int numRec = (fstatus.st_size - sizeof(struct CoordCrashInfo))/sizeof(CoordRecord);
  CoordRecord records[numRec];
  CoordRecord * rec = (CoordRecord *) (crashInfo + 1);
  int i;
  for (i = 0; i < numRec; i++) {
    CoordRecord * r = rec + i;
    records[i] = *r;
  }
  
  int j;
  uint32_t curTid = 0;

  for (i=0; i< MAX_TRANS; i++) {
    curTid = crashInfo->workerInfo[i].tid;

    for (j = numRec - 1; j >= 0; j--) {
      CoordRecord r = records[j];
      if(r.tid == curTid){
        if(r.msgID == (DECIDE_ABORT || DECIDE_COMMIT)) {
          break;
        } else if (r.msgID == COMMIT_REQ){
          abortTid = curTid;
          return 1;
        }
      }
    }
  }
  return 0;
}

/*
  Check from tranaction log whether transaction tid has been committed or not
*/
int isCommit(uint32_t curTid){
  struct stat fstatus;
  int fd;

  fd = open(logFileName, O_RDONLY);
  if (fstat(fd, &fstatus) < 0 ) {
    perror("Filestat failed");
    exit(1);
  }
  close(fd);

  int numRec = (fstatus.st_size - sizeof(struct CoordCrashInfo))/sizeof(CoordRecord);
  CoordRecord records[numRec];
  CoordRecord * rec = (CoordRecord *) (crashInfo + 1);
  int i;
  for (i = 0; i < numRec; i++) {
    CoordRecord * r = rec + i;
    records[i] = *r;
  }
  
  int j;
  for (j = numRec - 1; j >= 0; j--) {
    CoordRecord r = records[j];
    if (r.tid == curTid) {
      if(r.msgID == DECIDE_COMMIT){
        return 1;
      }
    }
  }
  return 0;
}

/*
  Write the record to transaction log
*/
int writeRecordToLog (CoordRecord rec) {
  FILE * fp;
  fp = fopen(logFileName, "ab");
  if (fp == NULL) {
    perror("fopen() failed");
    exit(1);
  }
  if (fwrite(&rec, sizeof(CoordRecord), 1, fp) == EOF) {
    perror("fputs() failed");
    exit(1);
  }
  fclose(fp);
  return 1;
}

/*
  Update the vector clock by merging it with the vector clock
  received from the message sender
*/
void mergeVectorClock(txMessage m) {
  int i, j;
  for (i = 0; i < MAX_NODES; i++) {
    if (m.vectorClock[i].nodeId == 0) {
      break;
    }
    for (j = 0; j < MAX_NODES; j++) {
      if ((m.vectorClock[i].nodeId == myVectorClock[j].nodeId)) {
        if (m.vectorClock[i].time > myVectorClock[j].time) {
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
  Clear transaction information after transaction tid has been committed
*/
void cleanWorkerInfo(uint32_t tid){
  int i,j;
  for (i = 0; i < MAX_TRANS; i++) {
    if(crashInfo->workerInfo[i].tid == tid){
      for(j = 0; j < MAX_NODES; j++) {
        crashInfo->workerInfo[i].workers[j].nodeId = 0;
        crashInfo->workerInfo[i].workers[j].port = 0;
        crashInfo->workerInfo[i].workers[j].voteYes = 0;
      }
      crashInfo->workerInfo[i].isCrash = 0;
      crashInfo->workerInfo[i].tid = 0;
      break;
    }
  }
}

/*
  Save all current transaction information to the beginning of the logfile
*/
int saveWorkerInfo(unsigned long port, uint32_t tid, uint32_t msgID, unsigned int nodeId, struct sockaddr_in workerAddr){
  
  if (crashInfo == 0) {
    perror("Object data could not be mapped in");
    return -1;
  }

  int i, j, index;
  for (i=0; i< MAX_TRANS; i++) {
    if (crashInfo->workerInfo[i].tid == tid) {
      if (msgID == BEGINTX) {
        // this transaction has already started reture failure
        return -1;
      }
      index = i;
      break;
    } else if (crashInfo->workerInfo[i].tid == 0) {
      if(msgID == JOINTX){
        // this transaction doesn't exist, cannot join;
        return -1;
      }
      crashInfo->workerInfo[i].tid = tid;
      index = i;
      break;
    }
  }
  // if it's a join msg, we add this worker to the transaction
  if (msgID == JOINTX) {
    for (j=0; j< MAX_NODES; j++) {
      if (crashInfo->workerInfo[index].workers[j].port == 0) {
        crashInfo->workerInfo[index].workers[j].port = port;
        crashInfo->workerInfo[index].workers[j].nodeId = nodeId;
        crashInfo->workerInfo[index].workers[j].workerAddr = workerAddr;
        break;
      }
    }
  }

  msync(crashInfo, sizeof(struct CoordCrashInfo), MS_SYNC);

  return 0;
}

/*
  Restore transaction manager's own vector clock when restoring state after a crash 
*/
int restoreVectorTime(){

  if (crashInfo == 0) {
    perror("Object data could not be mapped in");
    return -1;
  }

  int i;
  for (i = 0; i < MAX_NODES; i++) {
    myVectorClock[i].nodeId = crashInfo->vectorClock[i].nodeId;
    myVectorClock[i].time = crashInfo->vectorClock[i].time;
  }

  return 0;
}

/*
  Save the updated vector clock into the log file
*/
int saveVectorTime() {

  if (crashInfo == 0) {
    perror("Object data could not be mapped in");
    return -1;
  }

  int i;
  for(i=0; i<MAX_NODES; i++){
    crashInfo->vectorClock[i].nodeId= myVectorClock[i].nodeId;
    crashInfo->vectorClock[i].time = myVectorClock[i].time;
  }
  msync(crashInfo, sizeof(struct CoordCrashInfo), MS_SYNC);

  return 0;
}

/*
  Write manager local events into the ShiViz log
*/
void writeLocalEventToLog(char * fileName, uint32_t msgID, unsigned long port) {
  char logBuf[500];
  char vectorTimeBuf[500];
  char portBuf[10];
  switch (msgID) {
    case CRASH:
      sprintf(logBuf, "Locally CRASH");
      break;
    case RESTARTED:
      sprintf(logBuf, "Manager RESTARTED");
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
  Write events to ShiViz log based on the message to be sent or message that has been received
*/
void writeEventToLog(char * fileName, txMessage m, unsigned long port, int isSend, unsigned long toFromNodeId) {
  char logBuf[500];
  char vectorTimeBuf[500];
  char portBuf[10];
  switch (m.msgID) {
    case BEGINTX:
      sprintf(logBuf, isSend ? "Send BEGIN Transaction %lu to " : "Receive BEGIN Transaction %lu from ", m.tid);
      break;
    case JOINTX:
      sprintf(logBuf, isSend ? "Send JOIN Transaction %lu to " : "Receive JOIN Transaction %lu from ", m.tid);
      break;
    case JOIN_FAIL:
      sprintf(logBuf, isSend ? "SEND JOIN_FAIL Transaction %lu to " : "Receive JOIN_FAIL Transaction %lu from ", m.tid);
      break;
    case JOIN_SUCCESS:
      sprintf(logBuf, isSend ? "SEND JOIN_SUCCESS Transaction %lu to " : "Receive JOIN_SUCCESS Transaction %lu from ", m.tid);
      break;
    case BEGIN_FAIL:
      sprintf(logBuf, isSend ? "SEND BEGIN_FAIL Transaction %lu to " : "Receive BEGIN_FAIL Transaction %lu from ", m.tid);
      break;
    case BEGIN_SUCCESS:
      sprintf(logBuf, isSend ? "SEND BEGIN_SUCCESS Transaction %lu to " : "Receive BEGIN_SUCCESS Transaction %lu from ", m.tid);
      break;
    case PREPARE_COMMIT:
    case COMMIT:
      sprintf(logBuf, isSend ? "SEND PREPARE_COMMIT Transaction %lu to " : "Receive COMMIT Transaction %lu from ", m.tid);
      break;
    case VOTE_COMMIT:
    case DECIDE_COMMIT:
      sprintf(logBuf, isSend ? "SEND DECIDE_COMMIT Transaction %lu to " : "Receive VOTE_COMMIT Transaction %lu from ", m.tid);
      break;
    case DECIDE_ABORT:
    case VOTE_ABORT:
      sprintf(logBuf, isSend ? "SEND DECIDE_ABORT Transaction %lu to " : "Receive VOTE_ABORT Transaction %lu from ", m.tid);
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
  Construct a message to send from manager to worker
*/
void constructMessage(txMessage m, txMessage* newMsg, int msgKind, unsigned long nodeId) {
  int i;
  newMsg->msgID = msgKind;
  newMsg->tid = m.tid;
  if (nodeId == 0) {
    newMsg->nodeId = m.nodeId;
  } else {
    newMsg->nodeId = nodeId;
  }
  for (i = 0; i < MAX_NODES; i++) {
    newMsg->vectorClock[i].nodeId = myVectorClock[i].nodeId;
    newMsg->vectorClock[i].time = myVectorClock[i].time;
  }
}

/*
  Send message to worker
*/
int sendMsg(int sock, txMessage m, struct sockaddr_in from_addr) {
  unsigned int fromAddrLen = sizeof(from_addr);
  char msgBuf[sizeof(txMessage)];
  serializeWorkerMsg(msgBuf, m);
  if (sendto(sock, msgBuf, sizeof(txMessage), 0, (struct sockaddr *) &from_addr, fromAddrLen) != sizeof(txMessage)){
    perror("sendto() sent a different number of bytes than expected");
    return -1;
  }
}

/*
  Collect votes from workers of transaction and determine if we are still waiting for votes
*/
int collectVote(txMessage msg) {
  int i, j, commit;
  int transIndex = -1;
  for (i = 0; i < MAX_TRANS; i++) {
    if (crashInfo->workerInfo[i].tid == msg.tid) {
      for (j = 0; j < MAX_NODES; j++) {
        if (crashInfo->workerInfo[i].workers[j].nodeId == msg.nodeId) {
          if (msg.msgID == VOTE_COMMIT || VOTE_ABORT) {
            crashInfo->workerInfo[i].workers[j].voteYes = msg.msgID;
          }
          break;
        }
      }
      transIndex = i;
      break;
    }
  }

  // the transaction is no longer running, have previously decided to abort
  if (transIndex == -1) {
    if (isCommit(msg.tid) == 1) {
      return DECIDE_COMMIT;
    }else{
      return DECIDE_ABORT;
    }
  }

  for (i = 0; i < MAX_NODES; i++) {
    if (crashInfo->workerInfo[transIndex].workers[i].nodeId != 0) {
      int vote = crashInfo->workerInfo[transIndex].workers[i].voteYes;
      if (vote == VOTE_ABORT || vote == ABORT) {
        // if a worker voted abort, send abort
        return DECIDE_ABORT;
      } else if (vote == 0) {
        // some workers haven't voted yet
        return WAIT;
      } else {
        commit = DECIDE_COMMIT;
      }
    }
  }
  return commit;
}

/*
  Set the isCrash flag to indicate whether the manager should crash after making the decision
  for the specific transaction
*/
void setCrash(txMessage msg) {
  int i;
  for (i=0; i< MAX_TRANS; i++) {
    if (crashInfo->workerInfo[i].tid == msg.tid) {
      crashInfo->workerInfo[i].isCrash |= msg.isCrash;
      break;
    }
  }
  msync(crashInfo, sizeof(struct CoordCrashInfo), MS_SYNC);
}

/*
  Remove isCrash flag to prevent further crashes
*/
void resetCrash(txMessage msg) {
  int i;
  for (i=0; i< MAX_TRANS; i++) {
    if (crashInfo->workerInfo[i].tid == msg.tid) {
      crashInfo->workerInfo[i].isCrash = 0;
      break;
    }
  }
  msync(crashInfo, sizeof(struct CoordCrashInfo), MS_SYNC);
}

/*
  Get isCrash flag to determine if the manager should crash after making the decision
*/
int getCrash(txMessage msg) {
  int i;
  for (i=0; i< MAX_TRANS; i++) {
    if (crashInfo->workerInfo[i].tid == msg.tid) {
      return crashInfo->workerInfo[i].isCrash;
    }
  }
  return 0;
}

/*
  Send a message to all workers of a transaction
*/
void sendToAllWorkers(int sock, txMessage recvMsg, txMessage newMsg, int msgKind){
  int i, j;
  for (i = 0; i < MAX_TRANS; i++) {
    if (crashInfo->workerInfo[i].tid == recvMsg.tid) {
      for (j = 0; j < MAX_NODES; j++) { 
        uint32_t workerPort = crashInfo->workerInfo[i].workers[j].port;
        unsigned int workerId = crashInfo->workerInfo[i].workers[j].nodeId;
        struct sockaddr_in toAddr = crashInfo->workerInfo[i].workers[j].workerAddr;
        if (workerPort != 0) {
                  // send prepare to commit to all workers in this transaction
          updateMyVectorClock(port, &myVectorClock[0]);
          saveVectorTime();
          constructMessage(recvMsg, &newMsg, msgKind, workerId);
          writeEventToLog(ShivizFileName, newMsg, port, 1, newMsg.nodeId);
          sendMsg(sock, newMsg, toAddr);
        }
      }
    }
  }
}

/* 
  Handle messages received from worker
*/
void handleWorkerMsg(int sock) {
  struct sockaddr_in workerAddr;    /* Receiver address */
  unsigned int workerAddrLen;        /* Length of incoming message */
  char workerHost[10];
  unsigned long workerPortNo;
  char recvBuf[512];
  int recvMsgSize;
  int i, j, error;
  txMessage newMsg;
  int decision;
  //struct hostent *workerIP;  
  int rv; 

  struct pollfd ufds[1];
  ufds[0].fd = sock;
  ufds[0].events = POLLIN;

  CoordRecord rec;

  for(;;) {
    rv = poll(ufds, 1, 10000);

    if (rv == -1) {
      perror("poll"); // error occurred in poll()
    } else if (rv == 0) {
      // timeout 
      if (waitingForVotes() == 1) {
        // waiting for votes timed out. abort the transaction by sending decide_abort to everyone
        txMessage ourMsg;
        printf("Timeout occurred!  No data after 10 seconds.\n");
        rec.msgID = DECIDE_ABORT;
        rec.tid = abortTid;
        writeRecordToLog(rec);
        ourMsg.tid = abortTid;
        sendToAllWorkers(sock, ourMsg, newMsg, DECIDE_ABORT);
        cleanWorkerInfo(abortTid);
      }
    } else {
      workerAddrLen = sizeof(workerAddr);
      if ((recvMsgSize = recvfrom(sock, recvBuf, 512, 0, (struct sockaddr *) &workerAddr, &workerAddrLen)) > 0) {
        workerPortNo = ntohs(workerAddr.sin_port);
        txMessage msg = deserializeWorkerMsg(recvBuf);
        printWorkerMsg(msg);

        // received msg, update vectorClock, merge Clock, write event to shiviz
        updateMyVectorClock(port, &myVectorClock[0]);
        mergeVectorClock(msg);
        saveVectorTime();
        printf("Msg received from worker port: %d\n", workerPortNo);
        writeEventToLog(ShivizFileName, msg, port, 0, msg.nodeId); 
        switch (msg.msgID) {
          case BEGINTX:
            error = saveWorkerInfo(workerPortNo, msg.tid, msg.msgID, msg.nodeId, workerAddr);
            updateMyVectorClock(port, &myVectorClock[0]);
            saveVectorTime();
            if(error < 0){
              constructMessage(msg, &newMsg, BEGIN_FAIL, 0);
            } else {
              constructMessage(msg, &newMsg, BEGIN_SUCCESS, 0);
            }
            writeEventToLog(ShivizFileName, newMsg, port, 1, newMsg.nodeId);
            sendMsg(sock, newMsg, workerAddr);
            break;
          case JOINTX:
            error = saveWorkerInfo(workerPortNo, msg.tid, msg.msgID, msg.nodeId, workerAddr);
            updateMyVectorClock(port, &myVectorClock[0]);
            saveVectorTime();
            if (error < 0) {
              constructMessage(msg, &newMsg, JOIN_FAIL, 0);
            } else {
              rec.msgID = msg.msgID;
              rec.tid = msg.tid;
              rec.nodeId = msg.nodeId;
              writeRecordToLog(rec);
              constructMessage(msg, &newMsg, JOIN_SUCCESS, 0);
            }
            writeEventToLog(ShivizFileName, newMsg, port, 1, newMsg.nodeId);
            sendMsg(sock, newMsg, workerAddr);
            break;
          case COMMIT:
            rec.msgID = COMMIT_REQ;
            rec.tid = msg.tid;
            rec.nodeId = msg.nodeId;
            writeRecordToLog(rec);
            sendToAllWorkers(sock, msg, newMsg, PREPARE_COMMIT);
            break;
          case POLL_DECISION:
          case VOTE_COMMIT:
            if (msg.isCrash == 1) {
              setCrash(msg);
            }
            decision = collectVote(msg);
            if (decision == DECIDE_COMMIT) {
              if (msg.msgID == POLL_DECISION) {
                updateMyVectorClock(port, &myVectorClock[0]);
                saveVectorTime();
                constructMessage(msg, &newMsg, DECIDE_COMMIT, 0);
                writeEventToLog(ShivizFileName, newMsg, port, 1, newMsg.nodeId);
                sendMsg(sock, newMsg, workerAddr);
              } else {
                printf("send decide_commit \n");
                rec.msgID = DECIDE_COMMIT;
                rec.tid = msg.tid;
                rec.nodeId = msg.nodeId;
                writeRecordToLog(rec);
                if (getCrash(msg) == 1) {
                  resetCrash(msg);
                  updateMyVectorClock(port, &myVectorClock[0]);
                  saveVectorTime();
                  writeLocalEventToLog(ShivizFileName, CRASH, port);
                  cleanWorkerInfo(msg.tid);
                  _exit(0);
                }
                sendToAllWorkers(sock, msg, newMsg, DECIDE_COMMIT);
                cleanWorkerInfo(msg.tid);
              }
            }
            if (decision == DECIDE_ABORT) {
              printf("send decide_aobrt\n");
              updateMyVectorClock(port, &myVectorClock[0]);
              saveVectorTime();
              constructMessage(msg, &newMsg, DECIDE_ABORT, 0);
              writeEventToLog(ShivizFileName, newMsg, port, 1, newMsg.nodeId);
              sendMsg(sock, newMsg, workerAddr);
            }
            break;
          case VOTE_ABORT:
            if (msg.isCrash == 1) {
              setCrash(msg);
            }
            rec.msgID = DECIDE_ABORT;
            rec.tid = msg.tid;
            rec.nodeId = msg.nodeId;
            writeRecordToLog(rec);
            if (getCrash(msg) == 1) {
              resetCrash(msg);
              updateMyVectorClock(port, &myVectorClock[0]);
              saveVectorTime();
              writeLocalEventToLog(ShivizFileName, CRASH, port);
              cleanWorkerInfo(msg.tid);
              _exit(0);
            }
            sendToAllWorkers(sock, msg, newMsg, DECIDE_ABORT);
            cleanWorkerInfo(msg.tid);
          default:
            break;
        }
      }
    }
  }
}


void usage(char * cmd) {
  printf("usage: %s  portNum\n",
	 cmd);
}

/*
  Main method
  Initialize vector clocks and worker transaction info
  If manager is restarting after a crash, restore information from the transaction logs
*/
int main(int argc, char ** argv) {

  // This is some sample code feel free to delete it
  int            fileExist;
  char           dataObjectFileName[128];
  int            logfileFD;
  int            vectorLogFD;
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
    snprintf(logFileName, sizeof(logFileName), "TmanagerLog_%d.log", port);

    // check if logfile exist
    if( access( logFileName, F_OK ) != -1 ) {
      fileExist = 1;
    }

    logfileFD = open(logFileName, O_RDWR | O_CREAT | O_SYNC, S_IRUSR | S_IWUSR );
    if (logfileFD < 0 ) {
      char msg[256];
      snprintf(msg, sizeof(msg), "Opening %s failed", logFileName);
      perror(msg);
    }

    if(fstat(logfileFD, &fstatus) < 0 ) {
      perror("Filestat failed");
      return -2;
    }

    if (fstatus.st_size < sizeof(struct CoordCrashInfo)) {
     struct CoordCrashInfo  space;
     retVal = write(logfileFD, &space, sizeof(struct CoordCrashInfo));
     if (retVal != sizeof(struct CoordCrashInfo)) {
      printf("Some sort of writing error\n");
      return -3;
     }
    }

    crashInfo = mmap(NULL, 512, PROT_WRITE|PROT_READ, MAP_SHARED, logfileFD, 0);
  }

  snprintf(dataObjectFileName, sizeof(dataObjectFileName), 
    "TmanagerShiViz_%d.dat", port);
  strncpy(ShivizFileName, dataObjectFileName, 128);
  vectorLogFD = open(dataObjectFileName, 
   O_WRONLY | O_CREAT | O_APPEND | O_SYNC, S_IRUSR | S_IWUSR );
  if (vectorLogFD < 0 ) {
    char msg[256];
    snprintf(msg, sizeof(msg), "Opening %s failed", dataObjectFileName);
    perror(msg);
    err++;
  }
  
  if (err) {
    printf("%d transaction manager initialization error%sencountered, program exiting.\n",
	   err, err>1? "s were ": " was ");
    return -1;
  }
  
  
  printf("Starting up Transaction Manager on %d\n", port);
  printf("Port number:              %d\n", port);
  printf("Log file name:            %s\n", logFileName);

  int managerSock;
  managerSock = createSocket(port);
  if(managerSock < 0){
    return -1;
  }

  // initialize myVectorClock
  

  // write start to log if first time starting
  if (!fileExist) {
    writeStartToLog(ShivizFileName, port, 1);
    if (saveVectorTime() < 0) {
      return -1;
    }
    int i;
    for (i = 0; i < MAX_NODES; i++) {
      myVectorClock[i].nodeId = 0;
      myVectorClock[i].time = 0; 
    }
    myVectorClock[0].nodeId = port;
    myVectorClock[0].time = 1;

    //initialize workerinfo 
    int j;
    for (i = 0; i<MAX_TRANS; i++) {
      crashInfo->workerInfo[i].tid = 0;
      for (j=0; j<MAX_NODES; j++) {
        crashInfo->workerInfo[i].workers[j].nodeId = 0;
        crashInfo->workerInfo[i].isCrash = 0;
        crashInfo->workerInfo[i].workers[j].port = 0;
        crashInfo->workerInfo[i].workers[j].voteYes = 0;
      }
    }
  } else {
    // restore manager state
    if (restoreVectorTime() < 0) {
      return -1;
    }
    updateMyVectorClock(port, &myVectorClock[0]);
    saveVectorTime();
    writeLocalEventToLog(ShivizFileName, RESTARTED, port);

    findStateFromRecord();
    int k;
    for (k = 0; k < MAX_TRANS; k++) {
      if (state[k].isCommit == 0) {
        txMessage newMsg;
        txMessage msg;
        msg.tid = state[k].tid;
        sendToAllWorkers(managerSock, msg, newMsg, DECIDE_ABORT);
      }
    }

  }

  

  handleWorkerMsg(managerSock);

  // unmap crashInfo
  retVal = munmap(crashInfo, sizeof(struct CoordCrashInfo));
  if (retVal < 0) {
    perror("Unmap failed");
  }

  return 0;

}


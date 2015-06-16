#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "mpi.h"

/******* FG-MPI Boilerplate begin *********/
#include "fgmpi.h"
int bully( int argc, char** argv ); /*forward declaration*/
FG_ProcessPtr_t binding_func(int argc __attribute__ ((unused)), 
           char** argv __attribute__ ((unused)), 
           int rank __attribute__ ((unused))){
    return (&bully);
}

FG_MapPtr_t map_lookup(int argc __attribute__ ((unused)), 
           char** argv __attribute__ ((unused)), 
           char* str __attribute__ ((unused))){ 
    return (&binding_func);
}

int main( int argc, char *argv[] )
{
    FGmpiexec(&argc, &argv, &map_lookup);    
    return (0);
}
/******* FG-MPI Boilerplate end *********/

// This is where you should start, add or change any code in here as you like
// Note that bully code is simply meant to show you how to use MPI and you 
// probably want to gut most of it. In this example, the defines below are used as 
// TAGs, but you will probably want to put them into the buffer.  

#define  ELECTION_ID   20
#define  AYA_ID        21
#define  IAA_ID        22
#define  COORD_ID      23
#define  ANSWER_ID     24
#define  CLOCK_ID      25   // tag id for clock tick (node 0)
#define  IAM_DEAD_JIM  998
#define  IAM_ALIVE     1000
#define  LEADER_DEAD   1001 // tag id for leader death to lower nodes
#define  TERMINATE     999

/*
  Construct the buffer for the message
  eventID: tag ID of event
  dlcTime: current DLC time at the node sending message
*/
void constructMsg(int * buffer, int eventID, int dlcTime) {
  buffer[0] = eventID;
  buffer[1] = dlcTime;
}

/*
  Print Events (in normal or debug mode)
  dlcTime: current DLC time at the node that is sending/receiving message
  eventID: tag ID of event being sent/received
  sender: the node that is sending/sent the message
  receiver: the node to receive/has received the message
  isDebug: flag to toggle debug information printing
  isSend: flag to indicate whether event to be printed is a send or receive
*/
void eventPrinting(int dlcTime, int eventID, int sender, int receiver, int isDebug, int isSend) {
  switch (eventID) {
    case ELECTION_ID:
      if (isSend) {
        if (isDebug) {
          printf("[Debug] N%d sends Elect to N%d. DLC time: %d\n", sender, receiver, dlcTime);
        }
        printf("Node Printing: N%d, Event ID: ELECTION, DLC time: %d\n", sender, dlcTime);
      } else {
        if (isDebug) {
          printf("[Debug] N%d receives Elect from N%d. DLC time: %d\n", receiver, sender, dlcTime);
        }
      }
      break;
    case ANSWER_ID:
      if (isSend) {
        if (isDebug) {
          printf("[Debug] N%d sends Answer to N%d. DLC time: %d\n", sender, receiver, dlcTime);
        }
        printf("Node Printing: N%d, Event ID: PRIV_ANSWER, DLC time: %d\n", sender, dlcTime);
      } else {
        if (isDebug) {
          printf("[Debug] N%d receives Answer from N%d. DLC time: %d\n", receiver, sender, dlcTime);
        }
      }
      break;
    case COORD_ID:
      if (isSend) {
        if (isDebug) {
          printf("[Debug] N%d sends COORD to N%d. DLC time: %d\n", sender, receiver, dlcTime);
        }
        printf("Node Printing: N%d, Event ID: LEADER, DLC time: %d\n", sender, dlcTime);
      } else {
        if (isDebug) {
          printf("[Debug] N%d receives COORD from N%d. DLC time: %d\n", receiver, sender, dlcTime);
        }
      }
      break;
    case AYA_ID:
      if (isSend) {
        if (isDebug) {
          printf("[Debug] N%d sends AYA to N%d. DLC time: %d\n", sender, receiver, dlcTime);
        }
        printf("Node Printing: N%d, Event ID: PRIV_AYA, DLC time: %d\n", sender, dlcTime);
      } else {
        if (isDebug) {
          printf("[Debug] N%d receives AYA from N%d. DLC time: %d\n", receiver, sender, dlcTime);
        }
      }
      break;
    case IAA_ID:
      if (isSend) {
        if (isDebug) {
          printf("[Debug] N%d sends IAA to N%d. DLC time: %d\n", sender, receiver, dlcTime);
        }
        printf("Node Printing: N%d, Event ID: PRIV_IAA, DLC time: %d\n", sender, dlcTime);
      } else {
        if (isDebug) {
          printf("[Debug] N%d receives IAA from N%d. DLC time: %d\n", receiver, sender, dlcTime);
        }
      }
      break;
    case IAM_DEAD_JIM:
      if (isSend) {
        if (isDebug) {
          printf("[Debug] N%d sends DEAD to N%d. DLC time: %d\n", sender, receiver, dlcTime);
        }
        printf("Node Printing: N%d, Event ID: DEAD, DLC time: %d\n", sender, dlcTime);
      } else {
        if (isDebug) {
          printf("[Debug] N%d receives DEAD from N%d. DLC time: %d\n", receiver, sender, dlcTime);
        }
      }
      break;
    case IAM_ALIVE:
      if (isSend) {
        if (isDebug) {
          printf("[Debug] N%d sends IAM_ALIVE to N%d. DLC time: %d\n", sender, receiver, dlcTime);
        }
        printf("Node Printing: N%d, Event ID: ALIVE, DLC time: %d\n", sender, dlcTime);
      } else {
        if (isDebug) {
          printf("[Debug] N%d receives IAM_ALIVE from N%d. DLC time: %d\n", receiver, sender, dlcTime);
        }
      }
      break;
    case LEADER_DEAD:
      printf("Node Printing: N%d, Event ID: LEADERDEAD, DLC time: %d\n", sender, dlcTime);
      break;
    default:
      break;
  }
}

/*
  Handle the messages to be sent or received by all nodes with rank > 0 (all nodes except for clock node 0)
  size: number of nodes
  rank: node id of this node
  isDebug: flag to toggle debug printing
  timeoutValue: number of seconds to wait before timeout
  AYATime: average amount of seconds that a node waits before sending AYA to the coordinator
  sendFailure: probability that upon receipt of an AYA that the coordinator will declare itself dead
  returnToLife: average amount of time in seconds that a dead node waits before coming back to life 
*/
int handleNode(int size, int rank, int isDebug, int timeoutValue, int AYATime, int sendFailure, int returnToLife) {
    int sbuffer[2];
    int sbuff_size = 2;
    int buff_size = 2;
    // int rootNode = 0;
    int i;
    int buffer[2];

    int tick = 0;
    int AYATick = 0;
    int dlcTime = 0;

    int lastSent = 0;
    int lastReceived = 0;
    int AYARate = 0;
    int coordNode = 0;

    int coordDead = 0;
    double deadTime = 0;
    int coordReturnToLife = 0;

    MPI_Status status;
    MPI_Request req;
    MPI_Request sReq;
    int flag;

    // Start election by sending elect msg to all higher nodes
    if (rank == 1) {
     for (i = rank + 1; i < size; i++) {
        dlcTime++;
        constructMsg(buffer, ELECTION_ID, dlcTime);
        eventPrinting(dlcTime, ELECTION_ID, rank, i, isDebug, 1);
        MPI_Send(&buffer, buff_size, MPI_INT, i, ELECTION_ID, MPI_COMM_WORLD);
        lastSent = ELECTION_ID;
      }
    }

    // keep receiving and ACKING messages until a terminate one is received
    for (;;) { 
      MPI_Irecv(&sbuffer, sbuff_size, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &req);
      MPI_Wait(&req, &status);
      // Receive the clock tick from rootNode 0
      if (status.MPI_TAG == CLOCK_ID) {
        tick++;
        AYATick++;
        if (isDebug) {
          printf("[Debug] %d GOTTA a message of contents %d. The clock has ticked %d times.\n", rank, sbuffer[0], tick);
        }
        if ((AYARate == 0 || (AYATick % AYARate == 0)) && coordNode != 0) {
          int j;
          for (j = 0; j < 10; j++) {
            int rn;
            rn = random();
            AYARate = rn % (2*AYATime);
          }
          lastSent = AYA_ID;
          dlcTime++;
          constructMsg(buffer, AYA_ID, dlcTime);
          eventPrinting(dlcTime, AYA_ID, rank, coordNode, isDebug, 1);
          MPI_Isend(&buffer, buff_size, MPI_INT, coordNode, AYA_ID, MPI_COMM_WORLD, &sReq);
          MPI_Test(&sReq, &flag, &status);
          if (!flag) {
            MPI_Cancel(&sReq);
            MPI_Request_free(&sReq);
          }
        }
      } else {
        tick = 0;
        // set dlcTime to the bigger of the received and self
        dlcTime = sbuffer[1] > dlcTime ? sbuffer[1] : dlcTime;
        if(isDebug){
          printf("old dlctime is %d\n", sbuffer[1]);
        }
        eventPrinting(dlcTime, status.MPI_TAG, status.MPI_SOURCE, rank, isDebug, 0);
        switch (status.MPI_TAG) {
          case ELECTION_ID:
            //printf("ELECTION_ID at rank %d from %d\n", rank, status.MPI_SOURCE);
            dlcTime++; 
            constructMsg(buffer, ANSWER_ID, dlcTime);
            eventPrinting(dlcTime, ANSWER_ID, rank, status.MPI_SOURCE, isDebug, 1);
            MPI_Isend(&buffer, buff_size, MPI_INT, status.MPI_SOURCE, ANSWER_ID, MPI_COMM_WORLD, &sReq);
            MPI_Test(&sReq, &flag, &status);
            if (!flag) {
                MPI_Cancel(&sReq);
                MPI_Request_free(&sReq);
            }
            lastReceived = ELECTION_ID;
            // If I send a election last time, don't send anymore.
            if (lastSent == ELECTION_ID) {
              break;
            }
            lastSent = 0;
            for (i = rank + 1; i < size; i++) {
              dlcTime++;
              constructMsg(buffer, ELECTION_ID, dlcTime);
              eventPrinting(dlcTime, ELECTION_ID, rank, i, isDebug, 1);
              MPI_Isend(&buffer, buff_size, MPI_INT, i, ELECTION_ID, MPI_COMM_WORLD, &sReq);
              lastSent = ELECTION_ID;
              MPI_Test(&sReq, &flag, &status);
              if (!flag) {
                MPI_Cancel(&sReq);
                MPI_Request_free(&sReq);
              }
            }
            if (lastSent == 0) {
              for (i = 1; i < rank; i++) {
                dlcTime++;
                constructMsg(buffer, COORD_ID, dlcTime);
                eventPrinting(dlcTime, COORD_ID, rank, i, isDebug, 1);
                MPI_Isend(&buffer, buff_size, MPI_INT, i, COORD_ID, MPI_COMM_WORLD, &sReq);
                lastSent = COORD_ID;
                MPI_Test(&sReq, &flag, &status);
                if (!flag) {
                  MPI_Cancel(&sReq);
                  MPI_Request_free(&sReq);
                }
              }
            }
            break;
          case ANSWER_ID:
            if (lastReceived == COORD_ID) {
              lastSent = 0;
              break;
            }
            if (lastSent == ELECTION_ID) {        
              lastReceived = ANSWER_ID;
              lastSent = 0;
            }
            //printf("ANSWERID at rank %d from %d\n", rank, status.MPI_SOURCE);
            break;
          case COORD_ID:
            //printf("COORDID at rank %d from %d\n", rank, status.MPI_SOURCE);
            lastReceived = COORD_ID;
            coordNode = status.MPI_SOURCE;
            break;
          case AYA_ID:
            lastReceived = AYA_ID;
            lastSent = IAA_ID;
            dlcTime++;
            double curTime;

            int rn = random() % 100;
            if (rn >= sendFailure) {
              deadTime = MPI_Wtime();
              constructMsg(buffer, IAA_ID, dlcTime);
              eventPrinting(dlcTime, IAA_ID, rank, status.MPI_SOURCE, isDebug, 1);
              MPI_Isend(&buffer, buff_size, MPI_INT, status.MPI_SOURCE, IAA_ID, MPI_COMM_WORLD, &sReq);
              MPI_Test(&sReq, &flag, &status);
              if (!flag) {
                MPI_Cancel(&sReq);
                MPI_Request_free(&sReq);
              }
            } else {
              eventPrinting(dlcTime, IAM_DEAD_JIM, rank, status.MPI_SOURCE, isDebug, 1);
              coordDead = 1;
              deadTime = MPI_Wtime();
              
              // if coordinator is dead.. wait till returnToLife time is up
              while (coordDead) {
                curTime = MPI_Wtime();
                //printf("curTime: %f, deadTime: %f \n", curTime, deadTime );
                if (curTime - deadTime > returnToLife) {
                  if (isDebug) {
                    printf("N%d came back to life!!!!!!!!!!!!!\n", rank);
                  }
                  eventPrinting(dlcTime, IAM_ALIVE, rank, status.MPI_SOURCE, isDebug, 1);
                  coordDead = 0;
                  deadTime = -1;
                  coordReturnToLife = 1;
                  lastSent = 0;
                  lastReceived = 0;
                  break;
                } else {
                  if (isDebug) {
                    printf("N%d is still dead!!!!!!!!!!!!!\n", rank);
                  }
                  MPI_Irecv(&sbuffer, sbuff_size, MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &req);
                  MPI_Wait(&req, &status);
                }
              }

              if (coordReturnToLife) {
                // first time returning to life do nothing 
                coordReturnToLife = 0;
                break;
              }

            }
            break;
          case IAA_ID:
            lastReceived = IAA_ID;
            break;
          case IAM_DEAD_JIM:

            break;
          default:
            break;
        }
      }

      if ((tick % timeoutValue) == 0 && tick != 0) {
        if(isDebug){
          printf("++++++++TIMEOUT at RANK %d\n", rank);
        }
        switch (lastSent) {
          case 0:
            if (lastReceived == 0 ||(lastReceived == ANSWER_ID && (tick % (size-1)*timeoutValue == 0))) {
              for (i = rank + 1; i < size ; i++) {
                dlcTime++; //increment time before a send
                buffer[0] = ELECTION_ID;
                buffer[1] = dlcTime;
                eventPrinting(dlcTime, ELECTION_ID, rank, i, isDebug, 1);
                MPI_Isend(&buffer, buff_size, MPI_INT, i, ELECTION_ID, MPI_COMM_WORLD, &sReq);
                lastSent = ELECTION_ID;
                lastReceived = 0;
                MPI_Test(&sReq, &flag, &status);
                if (!flag) {
                  MPI_Cancel(&sReq);
                  MPI_Request_free(&sReq);
                }
              }
              tick = 0;
              coordNode = 0;
            }

            if (lastReceived == ELECTION_ID) { 
               for (i = 1; i < rank; i++) {
                dlcTime++;
                constructMsg(buffer, COORD_ID, dlcTime);
                eventPrinting(dlcTime, COORD_ID, rank, i, isDebug, 1);
                MPI_Isend(&buffer, buff_size, MPI_INT, i, COORD_ID, MPI_COMM_WORLD, &sReq);
                lastSent = COORD_ID;
                MPI_Test(&sReq, &flag, &status);
                if (!flag) {
                  MPI_Cancel(&sReq);
                  MPI_Request_free(&sReq);
                }
              }
              tick = 0;
              coordNode = 0;
            }
            break;
          case ELECTION_ID:
            for (i = 1; i < rank; i++) {
              dlcTime++;
              constructMsg(buffer, COORD_ID, dlcTime);
              eventPrinting(dlcTime, COORD_ID, rank, i, isDebug, 1);
              MPI_Isend(&buffer, buff_size, MPI_INT, i, COORD_ID, MPI_COMM_WORLD, &sReq);
              lastSent = COORD_ID;
              MPI_Test(&sReq, &flag, &status);
              if (!flag) {
                MPI_Cancel(&sReq);
                MPI_Request_free(&sReq);
              }
            }
            tick = 0;
            coordNode = 0;
            break;
          case AYA_ID:
            // we timedout after sending AYA, coord is dead
            eventPrinting(dlcTime, LEADER_DEAD, rank, 0, isDebug, 1);
            
            for (i = rank + 1; i < size; i++) {
              dlcTime++; //increment time before a send
              buffer[0] = ELECTION_ID;
              buffer[1] = dlcTime;
              eventPrinting(dlcTime, ELECTION_ID, rank, i, isDebug, 1);
              MPI_Isend(&buffer, buff_size, MPI_INT, i, ELECTION_ID, MPI_COMM_WORLD, &sReq);
              lastSent = ELECTION_ID;
              MPI_Test(&sReq, &flag, &status);
              if (!flag) {
                MPI_Cancel(&sReq);
                MPI_Request_free(&sReq);
              }
            }
            tick = 0;
            coordNode = 0;
            break;
          default:
            break;
        }
      }

      if (sbuffer[0] == TERMINATE) {
        break;
      }
    }
    return 1;
}

/*
  Handle the sending of the clock tick to the other nodes
  The Clock node is started node 0
  size: number of nodes
*/
int handleClock(int size) {
  int i;
  int buffer[2]; 
  int buff_size = 1;
  MPI_Request req;
  MPI_Status status;
  for (;;) {
    // Send clock tick to rest of the world
    for (i = 1; i < size; i++) {
      buffer[0] = CLOCK_ID;
      MPI_Isend(&buffer, buff_size, MPI_INT, i, CLOCK_ID, MPI_COMM_WORLD, &req);
      MPI_Wait(&req, &status);
    }
    // Sleep for one second
    MPIX_Usleep(1000000);
  }
}

void usage(char * cmd, int rank) {
  if (0 ==  rank) 
    printf("Usage mpirun -nfg X -n Y %s Z\n where X*Y must be >= Z\n", cmd);
}

/*
  Print usage of the bully program
*/
void usageBully(char * cmd) {
  printf("usage: %s  isDebug timeoutVal AYATime sendFailure returnToLife\n", cmd);
}

/*
  Main method of the bully program
  Parse all command line arguments to bully program and start the nodes
*/
int bully( int argc, char** argv ) {
  int rank,            // This is analogous to a process id/port number 
                       // The values start a 0 and go to size - 1
    size,              // The total number of process across all processors
    coSize,            // The number of processes co-located in your address space
    sRank;             // The ID of the first process in your address space

  
  /* From the file mpi.h this is the definition for MPI_status, with my comments on the 
     fields


     typedef struct MPI_Status {
     int count;          // Seems to be the count, in bytes of the data received
     int cancelled;      // Indication of whether of not the operation was cancelled.
                         // this probably only applies if you are doing non-blocking 
                         // operations
     int MPI_SOURCE;     // ID of the source
     int MPI_TAG;        // The actual message tag
     int MPI_ERROR;      /  The error code if there was one
     
     } MPI_Status;

  */     

  // Calls to initialize the system and get the data you will need 
  // You probably don't need to change any of this. 
  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPIX_Get_collocated_size(&coSize);
  MPIX_Get_collocated_startrank(&sRank);
  
  int isDebug;
  int timeoutValue;
  int AYATime;
  int sendFailure;
  int returnToLife;

  if (argc != 6) {
    usageBully(argv[0]);
    return -1;
  }

  char * end;
  int err = 0;

  isDebug = strtoul(argv[1], &end, 10);
  if (argv[1] == end) {
    printf("isDebug value conversion error\n");
    err++;
  }

  timeoutValue = strtoul(argv[2], &end, 10);
  if (argv[2] == end) {
    printf("Timeout value conversion error\n");
    err++;
  }

  AYATime = strtoul(argv[3], &end, 10);
  if (argv[3] == end) {
    printf("AYATime conversion error\n");
    err++;
  }

  sendFailure = strtoul(argv[4], &end, 10);
  if (argv[4] == end) {
    printf("sendFailure conversion error\n");
    err++;
  }

  returnToLife = strtoul(argv[5], &end, 10);
  if (argv[5] == end) {
    printf("returnToLife conversion error\n");
    err++;
  }

  if (err) {
    printf("%d conversion error%sencountered, program exiting.\n", err, err > 1 ? "s were ": " was ");
    return -1;
  }

  printf("Debug output?:            %d\n", isDebug);
  printf("Timeout value:            %d\n", timeoutValue);
  printf("AYATime:                  %d\n", AYATime);
  printf("Send failure:             %d\n", sendFailure);
  printf("Return to life:           %d\n", returnToLife);

  printf("Hello! I am rank %d of %d co-located size is %d, start rank %d \n", 
        rank, size, coSize, sRank);
  srandom(time(NULL));
  if (rank == 0) {
    // handle the clock node
    handleClock(size);
  } else {
    // handle the 'normal' node
    handleNode(size, rank, isDebug, timeoutValue, AYATime, sendFailure, returnToLife);
  }
  
  MPI_Finalize();
  return(0);
}

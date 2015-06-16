/*
The setup code are taken from the book <TCP/IP sockets in C>
The code that i've modified are my own work.
*/

#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), bind(), and connect() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_ntoa() */
#include <stdlib.h>     /* for atoi() and exit() */
#include <string.h>     /* for memset() */
#include <unistd.h>     /* for close() */
#include <pthread.h>        /* for POSIX threads */

#include <stdio.h>  /* for perror() */
#include <stdlib.h> /* for exit() */

#include <sys/sysinfo.h>
#define RCVBUFSIZE 32   /* Size of receive buffer */
#define MAXPENDING 5    /* Maximum outstanding connection requests */

pthread_mutex_t mutexnum;
uint32_t numThread;

void *ThreadMain(void *arg);            /* Main program of a thread */
void DieWithError(char *errorMessage);  /* Error handling function */
void HandleTCPClient(int clntSocket);   /* TCP client handling function */
int CreateTCPServerSocket(unsigned short port); /* Create TCP server socket */
int AcceptTCPConnection(int servSock);  /* Accept TCP connection request */
void manageBuffer(char *tempBuffer, char* myBuffer, int my_char);

/* Structure of arguments to pass to client thread */
struct ThreadArgs
{
    int clntSock;                      /* Socket descriptor for client */
};

int main(int argc, char **argv)
{
    int servSock;                    /* Socket descriptor for server */
    int clntSock;                    /* Socket descriptor for client */
    unsigned short echoServPort;     /* Server port */
    int max;
    pthread_t threadID;              /* Thread ID from pthread_create() */
    struct ThreadArgs *threadArgs;   /* Pointer to argument structure for thread */

    if (argc != 3)     /* Test for correct number of arguments */
    {
        fprintf(stderr,"Usage:  %s <Max num of Clients> <Server Port>\n", argv[0]);
        exit(1);
    }
    numThread = 0;

    echoServPort = atoi(argv[2]);  /* Second arg:  local port */
    max = atoi(argv[1]);    	   /* First arg: maximum number of clients

    /* Creates the socket to listen on */
    servSock = CreateTCPServerSocket(echoServPort);

    for (;;) /* run forever */
    {
        /* Accept a new connection from a client */
        clntSock = AcceptTCPConnection(servSock);

        if (numThread < max){
        /* Create separate memory for client argument */
        if ((threadArgs = (struct ThreadArgs *) malloc(sizeof(struct ThreadArgs))) 
               == NULL)
            DieWithError("malloc() failed");
        threadArgs -> clntSock = clntSock;

        /* Create client thread */
        if (pthread_create(&threadID, NULL, ThreadMain, (void *) threadArgs) != 0)
            DieWithError("pthread_create() failed");

        pthread_mutex_lock(&mutexnum);
        numThread++;
        printf("Number of Connected thread: %d\n", numThread);
        pthread_mutex_unlock(&mutexnum);

        printf("with thread %ld\n", (long int) threadID);
	}
	else
	{
	close(clntSock);
	}
    }
    /* NOT REACHED */
}

void *ThreadMain(void *threadArgs)
{
    int clntSock;                   /* Socket descriptor for client connection */

    /* Guarantees that thread resources are deallocated upon return */
    pthread_detach(pthread_self()); 

    /* Extract socket file descriptor from argument */
    clntSock = ((struct ThreadArgs *) threadArgs) -> clntSock;
    free(threadArgs);              /* Deallocate memory for argument */

    HandleTCPClient(clntSock);

    close(clntSock);
    pthread_mutex_lock(&mutexnum);
    numThread--;
    printf("Number of Connected thread: %d\n", numThread);
    pthread_mutex_unlock(&mutexnum);

    return (NULL);
}

void DieWithError(char *errorMessage)
{
    perror(errorMessage);
    exit(1);
}

int AcceptTCPConnection(int servSock)
{
    int clntSock;                    /* Socket descriptor for client */
    struct sockaddr_in echoClntAddr; /* Client address */
    unsigned int clntLen;            /* Length of client address data structure */

    /* Set the size of the in-out parameter */
    clntLen = sizeof(echoClntAddr);
    
    /* Wait for a client to connect */
    if ((clntSock = accept(servSock, (struct sockaddr *) &echoClntAddr, 
           &clntLen)) < 0)
        DieWithError("accept() failed");
    
    /* clntSock is connected to a client! */
    
    printf("Handling client %s\n", inet_ntoa(echoClntAddr.sin_addr));

    return clntSock;
}

int CreateTCPServerSocket(unsigned short port)
{
    int sock;                        /* socket to create */
    struct sockaddr_in echoServAddr; /* Local address */

    /* Create socket for incoming connections */
    if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
        DieWithError("socket() failed");
      
    /* Construct local address structure */
    memset(&echoServAddr, 0, sizeof(echoServAddr));   /* Zero out structure */
    echoServAddr.sin_family = AF_INET;                /* Internet address family */
    echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    echoServAddr.sin_port = htons(port);              /* Local port */

    /* Bind to the local address */
    if (bind(sock, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
        DieWithError("bind() failed");

    /* Mark the socket so it will listen for incoming connections */
    if (listen(sock, MAXPENDING) < 0)
        DieWithError("listen() failed");

    return sock;
}

void manageBuffer(char *tempBuffer, char* myBuffer, int my_char)
{
    bzero(tempBuffer, RCVBUFSIZE);
    memmove(tempBuffer, myBuffer+my_char /*-1*/,strlen(myBuffer)-my_char/*+1*/);
    bzero(myBuffer, RCVBUFSIZE);
    strncpy(myBuffer, tempBuffer, strlen(tempBuffer));
}

void HandleTCPClient(int clntSocket)
{
    char echoBuffer[RCVBUFSIZE];        /* Buffer for echo string */
    int recvMsgSize;                    /* Size of received message */
    struct sysinfo info;
    int sum = 0;
    char myBuffer[RCVBUFSIZE];
    char tempBuffer[RCVBUFSIZE];
    int invalid = -1;
    int numInvalid = 0;

    bzero(echoBuffer,RCVBUFSIZE);
    bzero(myBuffer,RCVBUFSIZE);
    /* Receive message from client */
    if ((recvMsgSize = recv(clntSocket, echoBuffer, RCVBUFSIZE, 0)) < 0){
        return;
    }

    /* Send received string and receive again until end of transmission */
    while (recvMsgSize > 0)      /* zero indicates end of transmission */
    {
        // append the new received msg to the end of myBuffer
   	    strcat(myBuffer, echoBuffer);
        int my_char;
        // loop myBuffer 
        for(my_char = 1; my_char <= strlen(myBuffer); my_char++)
        {
            // is it part of "uptime"?
            if(strncmp(myBuffer, "uptime", my_char) == 0)
            {
                /* get a uptime request */
                if(my_char == strlen("uptime"))
                {
                    sysinfo(&info);
                    uint32_t uptime = 0;
                    printf("Uptime = %i\n", info.uptime);
                    uptime = htonl(info.uptime);
                    if (send(clntSocket, &uptime, 4, 0) != 4)
                    {
                        perror("send() failed");
                        return;
                    }
                    manageBuffer(tempBuffer,myBuffer, my_char);
		            my_char = 0;
                }
                numInvalid = 0;

                 // cmd not completed, keep waiting
            }else if (strncmp(myBuffer, "load", my_char) == 0)
            {
                if(my_char == strlen("load"))
                {
                    printf("num of Thread: %i\n", numThread);
	    	        uint32_t loadResponce;
        	        loadResponce = htonl(numThread);
        	        if(send(clntSocket, &loadResponce, 4, 0) != 4)
                    {
                        perror("send() failed");
                        return;
                    }
                    manageBuffer(tempBuffer,myBuffer, my_char);
		            my_char = 0;
                }
                numInvalid = 0;

            }else if(strncmp(myBuffer, "exit", my_char) == 0){
                uint32_t exit_resp = 0;
                exit_resp = htonl(exit_resp);
                if(send(clntSocket, &exit_resp, 4, 0) != 4)
                {
                    perror("send() failed");
                    return;
                }
       		    printf("handle exit\n");
		        bzero(tempBuffer, RCVBUFSIZE);
		        bzero(myBuffer, RCVBUFSIZE);
                numInvalid = 0;
      		    return;
            }else if(isdigit(myBuffer[my_char -1]) || myBuffer[my_char-1] == ' ')
            {
                // If the previous character is not a number, it's invalid
                if((my_char > 1 && !isdigit(myBuffer[my_char -2])) || (my_char == 1 && myBuffer[my_char-1] == ' '))
                {
                    // invalid
                    manageBuffer(tempBuffer,myBuffer, my_char);
                    my_char = 0;   
                    if (send(clntSocket, &invalid, 4, 0) != 4){
                        perror("send() failed");
                        return;
                    }
                    numInvalid++;
                    if(numInvalid > 2)
                    {
                        return;
                    }
                }
                else if(myBuffer[my_char-1] == ' ') //if we see a space, return the sum
                {
                    uint32_t sum = 0;
                    const int len = strlen(myBuffer);
                    int i;
                    for(i = 0; i < my_char-1; i++) {
                        sum += myBuffer[i] - '0';
                    }
                    printf("sum = %i\n", sum);
                    sum = htonl(sum);
                    if (send(clntSocket, &sum, 4, 0) != 4){
                        perror("send() failed");
                        return;
                    }
                    printf("send sum to client\n");
                    manageBuffer(tempBuffer,myBuffer, my_char);
                    my_char = 0;
                    numInvalid = 0;
                }
                else
                {
                    numInvalid = 0; // keey wating
                }
            }else
            {
                // invalid
                bzero(tempBuffer, RCVBUFSIZE);
                if(my_char == 1)
                {
                    memmove(tempBuffer, myBuffer+my_char,strlen(myBuffer)-my_char);
                } else{
                    memmove(tempBuffer, myBuffer+my_char -1,strlen(myBuffer)-my_char+1);
                }
                bzero(myBuffer, RCVBUFSIZE);
                strncpy(myBuffer, tempBuffer, strlen(tempBuffer));
                my_char = 0;   
                numInvalid++;
                if (send(clntSocket, &invalid, 4, 0) != 4)
                {
                    perror("send() failed");
                    return;
                }
                if(numInvalid > 2)
                {
                    return;
                }
            }
        }

        /* See if there is more data to receive */
        bzero(echoBuffer,RCVBUFSIZE);
        if ((recvMsgSize = recv(clntSocket, echoBuffer, RCVBUFSIZE, 0)) < 0){
            return;
        }
    }
}

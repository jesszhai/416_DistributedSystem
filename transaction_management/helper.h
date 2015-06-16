#ifndef HELPER_H
#define HELPER_H 1

void writeToFile(char * logFileName, char * buf);
void writeStartToLog(char * logFileName, unsigned long port, unsigned long clock);

int createSocket(unsigned long port);
int createCmdSocket();
void printMsg(msgType m);
msgType deserializeMsg(char * buffer);
char * deserializeUnsignedInt(char * buffer, uint32_t * val);
char * deserializeInt(char * buffer, int32_t * val);
void serializeMsg(char * buffer, msgType m);
char * serializeInt(char * buffer, int32_t val);
char * serializeUnsignedInt(char * buffer, uint32_t val);

void serializeWorkerMsg(char * buffer, txMessage m);
void printWorkerMsg(txMessage m);
txMessage deserializeWorkerMsg(char * buffer);
void writeVectorTime(char * vectorTimeBuf, struct clock * vc, unsigned long port);
int updateMyVectorClock(unsigned long port, struct clock * vc);
void printRecord (Record r);
void printCoordRecord (CoordRecord r);

#endif
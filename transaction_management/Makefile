all: tmanager tworker cmd dumpObject


CLIBS=-pthread
CC=gcc
CPPFLAGS=
CFLAGS=-g -Werror-implicit-function-declaration -pedantic -std=c99 

helper.o: helper.h helper.c msg.h
	$(CC) -c $(CFLAGS) $(OPTFLAGS)  helper.c
tmanager.o: tmanager.h helper.h tmanager.c msg.h
	$(CC) -c $(CFLAGS) $(OPTFLAGS)  tmanager.c
tworker.o: tworker.h msg.h helper.h tworker.c
	$(CC) -c $(CFLAGS) $(OPTFLAGS)  tworker.c
dumpObject.o: helper.h msg.h tworker.h dumpObject.c
	$(CC) -c $(CFLAGS) $(OPTFLAGS)  dumpObject.c
cmd.o: helper.h msg.h cmd.c
	$(CC) -c $(CFLAGS) $(OPTFLAGS)  cmd.c

tworker: tworker.o helper.o
	$(CC) $(CFLAGS) -o tworker helper.o tworker.o

tmanager: tmanager.o helper.o
	$(CC) $(CFLAGS) -o tmanager tmanager.o helper.o

dumpObject: dumpObject.o helper.o
	$(CC) $(CFLAGS) -o dumpObject dumpObject.o helper.o

cmd: cmd.o helper.o
	$(CC) $(CFLAGS) -o cmd helper.o cmd.o

cleanlogs:
	rm -f *.log

cleanobjs:
	rm -f *.data

clean:
	rm -f *.o
	rm -f tmanager tworker cmd dumpObject

scrub: cleanlogs cleanobjs clean



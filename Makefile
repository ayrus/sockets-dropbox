PORT=31337

CFLAGS= -DPORT=$(PORT) -g -Wall 

all: dbclient dbserver

dbclient : dbclient.o wrapsock.o writen.o readn.o
	gcc ${CFLAGS} -o $@ $^

dbserver : dbserver.o wrapsock.o writen.o readn.o filedata.o
	gcc ${CFLAGS} -o $@ $^

%.o : %.c
	gcc ${CFLAGS} -c $<

clean: 
	rm *.o dbclient dbserver

#ifndef PORT
#define PORT 10000
#endif
#define LISTENQ 5

//Sleep time for the client.
#define WAITTIME 10

#define MAXNAME 64
#define MAXCLIENTS 10
#define MAXFILES 10

#define CHUNKSIZE 256

/* Following define the possible states of every client.
 * DEADCLIENT: The client is not active (has no open connection).
 * SYNC: The client is currently in its SYNC state.
 * GETFILE: The is currently sending the server a file to be written.
 */
#define DEADCLIENT -1
#define SYNC 0
#define GETFILE 1

struct login_message {
    char userid[MAXNAME];
    char dir[MAXNAME];
};

struct sync_message {
	char filename[MAXNAME];
	long int mtime;
	int size;
};



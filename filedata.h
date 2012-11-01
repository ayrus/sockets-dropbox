#include <time.h>
#include "message.h"

struct file_info {
    char filename[MAXNAME];
    time_t mtime;   //Last modified time of the file.
	int size; //Size of the file.
};

struct client_info {
    int sock;
    char userid[MAXNAME];
    char dirname[MAXNAME];
    struct file_info files[MAXFILES];
    int state;

	int refresh; //Flag - whether to refresh the last modified times in the file_info array.
	int sharing; //Flag - if the drectory is being shared.

	/* Following variables keep track of an open/pending
	 * file transfer from the client to the server */
	char get_filename[MAXNAME]; //The filename in process.
	int get_filename_size; //Size of the file.
	int get_filename_readcount; //Total bytes that have been received and written so far.
	long int get_filename_timestamp; //The last modified time of the file as reported by the client.
};
extern struct client_info clients[MAXCLIENTS];

void init();
int add_client(struct login_message s);
struct file_info *check_file(struct file_info *files, char *filename);


void display_clients();


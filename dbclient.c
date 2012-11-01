#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include <netdb.h>
#include <utime.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include "message.h"
#include "wrapsock.h"

/* Wrapper signatures */
ssize_t Readn(int fd, void *ptr, size_t nbytes);
void Writen(int fd, void *ptr, size_t nbytes);

/* Function signatures */
int setup(char *hostname, char *directory, char *user);
int server_connect(char *hostname);
void sync_files(char *directory, int soc);
void retrieve_new_files(int soc, char *directory);
void send_file(int soc, char *directory, char *file);
void get_file(int sock, char* directory, char* file, int size, long int timestamp);

/* Connect to a server given by a hostname '-h' with username 'u' and directory
 * '-d'. Synchronize files both ways with the server. Retrieve any new files on the
 * the server as well. Maintain the same state of files with the server for the
 * directory '-d'. Once synchronized, halt execution for a predifined time and
 * re-check the synchronization state as above.
 * @Return: 0 on successful execution.
 */ 
int main(int argc, char **argv){

	char ch;
    char *host = NULL, *dir = NULL, *user = NULL;

	 while((ch = getopt(argc, argv, "h:d:u:")) != -1) {
        switch (ch) {
        case 'h':
            host = optarg;
            break;
        case 'd':
            dir = optarg;
            break;
		case 'u':
			user = optarg;
			break;
        default:
            fprintf(stderr, "Usage: dbclient -h <server hostname> -d <directory> -u <user id>\n");
            exit(1);
        }
    }
     
	//Sanitize inputs.
	if(!host || !dir || !user){
		fprintf(stderr, "Usage: dbclient -h <server hostname> -d <directory> -u <user id>\n");
		exit(1);
	}
	
	//Connect to a server and log in.
	int soc = setup(host, dir, user);

	//Synchronize files once logged in.
	sync_files(dir, soc);

	return 0;
}

/* Create a socket and attempt to connect to a server given by hostname 'hostname'.
 * Return the socket at which the connection has been made to the server.
 * @Param: hostname the hostname of the server to conenct to.
 * @Return: soc the socket at which the connection to the server has been made.
 */
int server_connect(char *hostname){

	int soc;
    struct hostent *hp;
    struct sockaddr_in host;

    host.sin_family = PF_INET;
    host.sin_port = htons(PORT); 

    //Get the host address.               
    if ((hp = gethostbyname(hostname)) == NULL) {  
		fprintf(stderr, "Unknown host: %s\n", hostname);
		exit(1);
    }

    host.sin_addr = *((struct in_addr *)hp->h_addr);

	//Create a new socket
    soc = Socket(PF_INET, SOCK_STREAM, 0);

    //Attempt a connection to the server.
    if (Connect(soc, (struct sockaddr *)&host, sizeof(host)) == -1)
    {  
		exit(1); 
    }

	//Return the active socket to the server.
	return soc;
}

/* Establish a connection to a server given by the hostname 'hostname'.
 * Send login credentials (with the 'directory' and user name 'user) to 
 * the server once connected. Return the socket at which the connection is
 * active.
 * @Param: hostname the hostname of the server to connect to.
 * @Param: directory the directory used by this client for synchornization
 *			(user for the login message).
 * @Param: user the username of the user using this client for logging in.
 * @Rreturn: soc the socket at which the connection to the server is alive.
 */
int setup(char *hostname, char *directory, char *user){
	
	int soc;
	struct login_message handshake;

	//Establish a connection to the server.
	soc = server_connect(hostname);

	//Login to the server by sending a 'handshake' packet.
	strncpy(handshake.userid, user, MAXNAME);
	strncpy(handshake.dir, directory, MAXNAME);
    Writen(soc, &handshake, sizeof(handshake));

	printf("INFO: Logged into server: %s\n", hostname);

	//Return the socket at which the server is connected.
	return soc;

}

/* Synchronize the directory given by 'directory' with the server
 * connected at the socket 'soc'. Send a file if it is more recent on the
 * client. Retrieve a file if it is more recent on the server. Retrieve any
 * new file on the server which does not already exist in the directory.
 * Sleep execution for a predefined time and repeat the same as above.
 * @Param: directroy the directory to synchronize.
 * @Param: soc the socket at which the server is connected.
 * @Return: void.
 */
void sync_files(char *directory, int soc){

	DIR *dir;
	struct dirent *file;
	struct stat st;
	char fullpath[CHUNKSIZE];
	struct sync_message sync_packet;
	struct sync_message server_sync_packet;
	int packet_size = sizeof(sync_packet);
	int file_count, n;
	
	while(1){

		file_count = 0; //Number of files in the directory.

		if((dir = opendir(directory)) == NULL){
			perror("Opening directory: ");
			exit(1);
		}

		while(((file = readdir(dir)) != NULL) && file_count < MAXFILES){

				//Grab the full path to the file.
				strncpy(fullpath, directory, CHUNKSIZE);
				strcat(fullpath, "/");
				strncat(fullpath, file->d_name, CHUNKSIZE-strlen(fullpath));

				if(stat(fullpath, &st) != 0){
					perror("stat");
					exit(1);
				}

				//Check if this file is a regular file (ignores dot files/subdirectories).
				if(S_ISREG(st.st_mode)){
				
					//Prepare the respective sync packet to be sent.
					strncpy(sync_packet.filename, file->d_name, MAXNAME);
					sync_packet.mtime = (long int)st.st_mtime;
					sync_packet.size = (int)st.st_size;
		
					//Write the sync packet to the server.			
					Writen(soc, &sync_packet, packet_size);
		
					//Read the server's resposne to the sent sync packet.
					if((n = Readn(soc, &server_sync_packet, packet_size)) != packet_size){
						fprintf(stderr, "Communitation mismatch: Server did not acknowledge sync packet.\n");
						Close(soc);
						exit(1);
					}

					//Determine if this file has to be sycned either way.
					if(server_sync_packet.mtime < sync_packet.mtime){ //Client has a newer version.
						printf("TX: Sending file: %s\n", file->d_name);
						send_file(soc, directory, sync_packet.filename);
						printf("\tTX: Complete.\n");
					}else if(server_sync_packet.mtime > sync_packet.mtime){ //Server has a more recent version.
						printf("TX: Get file: %s\n", file->d_name);
						get_file(soc, directory, server_sync_packet.filename, server_sync_packet.size, server_sync_packet.mtime);
						printf("\tTX complete: Updating file %s.\n", server_sync_packet.filename);						
					}
				}
	
				file_count++; 
		}

		if(closedir(dir) == -1){
			perror("Closing directory: ");
			exit(1);
		}

		/* Once all files in this directory have been checked for their synchronization,
		 * check if the server has any new files, which this client does not and retrieve
		 * them if so.
		 */
		retrieve_new_files(soc, directory);

		printf("INFO: Sleeping\n");
		//Sleep for WAITTIME.
		sleep(WAITTIME);
	}

	Close(soc);	
}

/* Check and retrieve any new file on the server (pointed to by a connetion 
 * at socket 'soc') to directory 'directory'. Retrieve as long as the server 
 * acknowledges that there are no more new files.
 * @Param: soc the socket at which the server is connected to.
 * @Param: directory the directory where the file has to be retrieved to.
 * @Return: void.
 */
void retrieve_new_files(int soc, char *directory){

	struct sync_message empty_packet;
	struct sync_message server_sync_packet;
	int packet_size = sizeof(empty_packet);
	int n;
	int flag = 1; //A flag which is set to 1 as long as the server has some new files to send.

	//Prepare an 'empty' packet, to check status of new files on the server.
	strncpy(empty_packet.filename, "", MAXNAME);
	empty_packet.mtime = 0;
	empty_packet.size = 0;

	while(flag){

		//Write the empty packet.
		Writen(soc, &empty_packet, packet_size);

		//Read the server response.
		if((n = Readn(soc, &server_sync_packet, packet_size)) != packet_size){
			fprintf(stderr, "Communitation mismatch: Server did not acknowledge empty sync packet.\n");
			Close(soc);
			exit(1);
		}

		if(strlen(server_sync_packet.filename) == 0){
			//No more new files left on the server.
			printf("INFO: No more new files.\n");
			flag = 0;
		}else{
			//Server has a new file, retrieve it.
			printf("TX: Retrieving new file %s.\n", server_sync_packet.filename);
			get_file(soc, directory, server_sync_packet.filename, server_sync_packet.size, server_sync_packet.mtime);
			printf("\tTX complete: Retrieving new file %s.\n", server_sync_packet.filename);
		}
	}
}

/* Send a file 'file' present in the directory 'directory' by reading it in
 * CHUNKSIZE parts and writing to the socket 'soc'.
 * @Param: soc the socket to write the file to.
 * @Param: directory the directory where the file is present.
 * @Param: file the file to send.
 * @Return: void.
 */
void send_file(int soc, char* directory, char* file){
	FILE *fp;
	char fullpath[CHUNKSIZE];	
	char buffer[CHUNKSIZE];
	int bufsize = CHUNKSIZE;
	int i;

	//Grab the full path to the file.
	strncpy(fullpath, directory, CHUNKSIZE);
	strcat(fullpath, "/");
	strncat(fullpath, file, CHUNKSIZE-strlen(fullpath));

	if((fp = fopen(fullpath, "r")) == NULL) {
		perror("fopen on send file: ");
		exit(1);
    }

	//Read up to bufsize or EOF (whichever occurs first) from 'fp'.
	while((i = fread(buffer, 1, bufsize, fp))){
		if(ferror(fp)){
    		fprintf(stderr, "A read error occured.\n");
			Close(soc);
			exit(1);
		}

		//Write the 'i' bytes read from the file to the socket 'soc'.
		Writen(soc, &buffer, i);
	}

	if((fclose(fp))) {
		perror("fclose: ");
		exit(1);
    }
}

/* Retrieve a file 'file' of size 'size' by reading the contents of it from the 
 * socket 'sock' and write the file with the same name to directory 'directory'. 
 * Update the last modified time of this file to 'timestamp' on the filesystem.
 * @Param: sock the socket to read from.
 * @Param: directory the directory to write the file to.
 * @Param: size the size of the file being retrieved.
 * @Param: timestamp the last modified time to set to.
 * @Return: void.
 */
void get_file(int sock, char* directory, char* file, int size, long int timestamp){
	int readlength, readcount = 0, length;
	char buffer[CHUNKSIZE];
	char fullpath[CHUNKSIZE];
	FILE *fp;

	//Grab the full path to the file.
	strncpy(fullpath, directory, CHUNKSIZE);
	strcat(fullpath, "/");
	strncat(fullpath, file, CHUNKSIZE-strlen(fullpath));

	//Open the file. Overwite if it already exists.
	if((fp = fopen(fullpath, "w")) == NULL) {
		perror("fopen on get file: ");
		exit(1);
    }

	/* Length computes the size of the file to be read per read call.
	 * if the length happens to be more than CHUNKSIZE (256) then read only
	 * CHUNKSIZE at max for now.
	 */
	if((length = size) > CHUNKSIZE){
		length = CHUNKSIZE;
	}

	while( readcount < size ){

		/* Compute the reamining length to be read. If the reamining length happens
		 * to be more than CHUNKSIZE (256) then read only CHUNKSIZE at max for now
		 */
		length = size - readcount;
		if (length > CHUNKSIZE){
			length = CHUNKSIZE;
		}

		readlength = Readn(sock, &buffer, length);

		//Update how many bytes has been read yet.
		readcount += readlength;

		fwrite(buffer, readlength, 1, fp);
	
		//If there was an error with fwrite.
		if(ferror(fp)){
    		fprintf(stderr, "A write error occured.\n");
			Close(sock);
			exit(1);
		}
	}		
	
	if((fclose(fp))) {
		perror("fclose: ");
		exit(1);
    }	

	struct stat sbuf;
    struct utimbuf new_times;
	
	if(stat(fullpath, &sbuf) != 0) {
    	perror("stat");
    	exit(1);
    }
	
	//Update the last modified time to 'timestamp' for this file on the filesystem.
	new_times.actime = sbuf.st_atime; //Access time.
	new_times.modtime = (time_t)timestamp;
	
	if(utime(fullpath, &new_times) < 0) {
    	perror("utime");
    	exit(1);
    }
}

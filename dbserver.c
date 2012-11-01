#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <utime.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>    /* Internet domain header */
#include "wrapsock.h"
#include "filedata.h"

/* Wrapper signatures */

ssize_t Readline(int fd, void *ptr, size_t maxlen);
void Writen(int fd, void *ptr, size_t nbytes);
ssize_t Readn(int fd, void *ptr, size_t nbytes);

/* Function signatures */

void close_connection(int sock, struct client_info *client, fd_set *allset);
void process_client_request(int sock, struct client_info *client, struct sync_message received_packet, fd_set *allset);
int send_new_file(int sock, struct client_info *client);
void check_sharing(struct client_info *client, int client_slot);
void add_shared(struct client_info *client, char *filename);
void refresh_file_times(struct client_info *client);
void get_file(int sock, struct client_info *client, char *buffer, int length);
void send_file(int sock, char *directory, char *filename);

/* Set up the server socket. Create and bind a socket such that it listents
 * for incoming connections on this socket. Return the socket.
 * @Return: listenfd the socket at which the server is listening for clients.
 */
int set_up(){
	int listenfd;
	struct sockaddr_in servaddr;	
	int yes = 1;

    listenfd = Socket(AF_INET, SOCK_STREAM, 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port        = htons(PORT);

    if((setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)))
       == -1) {
        perror("setsockopt");
    }

	//Bind to 'listenfd'.
    Bind(listenfd, (struct sockaddr *) &servaddr, sizeof(servaddr));

	//Listen for incoming connections at listenfd.
    Listen(listenfd, LISTENQ);

	//Return the socket.
	return listenfd;
}

int main(int argc, char **argv) {
    int i, maxi, maxfd, connfd, sockfd;
    int nready;
    ssize_t	n;
    fd_set rset, allset;
    char buffer[CHUNKSIZE];
    
    struct sockaddr_in cliaddr;
	socklen_t clilen;
    clilen = sizeof(cliaddr);

	//Set up a bounded socket where incoming connections are being heard
	//-- and queued.
	int listenfd = set_up();
	
    maxfd = listenfd;
	//Present maximum index into the client's array.
    maxi = -1;

	//Inititalize the FD SET.
    FD_ZERO(&allset);

	//Set the 'listenfd' to be checked for in the set.
    FD_SET(listenfd, &allset);
    
	//Set up the client's information (populate with default values).
	init();

	printf("INFO: Server booted up.\n");

	/* Login message sent by each client */
	struct login_message handshake;
	int handshake_size = sizeof(handshake);

	struct sync_message received_packet;
	int received_packet_size = sizeof(received_packet);

	int client_slot, get_read_size;
	char path[CHUNKSIZE];

    for ( ; ; ) {
		rset = allset;					// make a copy because rset gets altered
		nready = Select(maxfd+1, &rset, NULL, NULL, NULL);
	
    	//Check for any nre connection.
		if (FD_ISSET(listenfd, &rset)) {	
			connfd = Accept(listenfd, (struct sockaddr *) &cliaddr, &clilen);

			//First message has to be a login message. Read it.
			if((n = Readn(connfd, &handshake, handshake_size)) != handshake_size){
				fprintf(stderr, "Fatal: Expecting handshake packet. Client sent malformed data.\n");
				exit(1);
			}

		  	if((client_slot = add_client(handshake)) >= 0){
				clients[client_slot].sock = connfd;
				clients[client_slot].state = SYNC;

				//Populate the flags with default values.
				clients[client_slot].refresh = 0;
				clients[client_slot].sharing = 0;

				strncpy(path, "server_files/", 14);
				strncat(path, handshake.dir, CHUNKSIZE-14);

				mkdir(path, S_IRWXU); //CHMOD 700; creates if it does not exist.
				
				printf("CONNECT: Accepted a new client: %s\n", handshake.userid);

				check_sharing(&clients[client_slot], client_slot);
			}else{
				//"Too many clients" error would have been raised.
				exit(1);
			}
		 
			//Add the client's socket to the descriptor.
			FD_SET(connfd, &allset);
			
			if (connfd > maxfd)
			  maxfd = connfd;	
			if (i > maxi)
			  maxi = i;	
			
			if (--nready <= 0)
			  continue;	/* no more readable descriptors */
		}
	
   	 	//Check the clients for data.
		for (i = 0; i <= maxi; i++) {
	
	    	if ((sockfd = clients[i].sock) < 0) //Not active.
		 		continue;
	   		if (FD_ISSET(sockfd, &rset)) {

				//Client 'clients[i]' has some data.
				if(clients[i].state == SYNC){

					if(clients[i].refresh){ 
						/* First reqeust after a complete cycle (see README). 
						 * Refresh times from the server's filesystem, if modified.
						 */
						refresh_file_times(&clients[i]);

						//Once refreshed, do not do so until a cycle is complete.
						clients[i].refresh = 0; 
					}

					if ((n = Readn(sockfd, &received_packet, received_packet_size)) <= 0) {
				    	//Client closed connection.
						close_connection(sockfd, &clients[i], &allset);
					}else{
						//A sync packet has been received, process it.
						process_client_request(sockfd, &clients[i], received_packet, &allset);
					}	

				}else if(clients[i].state == GETFILE){				

					//This client has an ongoing GETFILE transaction.
				
					/* red_read_size determines how many bytes to read from the socket, read at most
					 * CHUNKSIZE bytes.
					 */
					if((get_read_size = clients[i].get_filename_size - clients[i].get_filename_readcount) > CHUNKSIZE){
						get_read_size = CHUNKSIZE;
					}

					if ((n = Readn(sockfd, &buffer, get_read_size)) <= 0) {
						//Client closed connection.
						close_connection(sockfd, &clients[i], &allset);
					}else{
						//Write the file out on the server's file system.						
						get_file(sockfd, &clients[i], buffer, n);
					}
				}
			}

			if (--nready <= 0)
			    break;	/* no more readable descriptors */
	    }
	}    
}

/* Close a connection on sock 'sock' for a client 'client'. Remove the client's
 * socket from the set 'allset' of descriptors being checked for readiness and update
 * the client's status as DEADCLIENT.
 * @Param: sock the socket at which the client is connected.
 * @Param: client the associated client_info struct for the client.
 * @Param: allset the FD set associated with the server's select calls
 * @Return: void.
 */
void close_connection(int sock, struct client_info *client, fd_set *allset){
	
	Close(sock);
	//Clear the sock from the set of descriptions being checked.
	FD_CLR(sock, allset);

	client->sock = -1;
	client->state = DEADCLIENT;

	printf("DEAD CLIENT: Closed connection on user: %s\n", client->userid);
}

/* Process an incoming sync_message from the client. Checks if first the filename being
 * queried is being shared for the same directory with any other client. Send or retrieve
 * the file depending upon the last modified time information in the recevied sync_message.
 * If the sync_message packet is empty then check if there are any new files on the server
 * which are not present on the client. If so send at most one new file.
 * @Param: sock the socket at which the client is connected.
 * @Param: client the associated client_info struct for the client.
 * @Param received_packet the sync_message packet which the client sent.
 * @Param: allset the FD set associated with the server's select calls (used in an event to kill
 * 			a client's connection non-gracefully if the client has exceed MAXFILES files).
 * @Return: void.
 */
void process_client_request(int sock, struct client_info *client, struct sync_message received_packet, fd_set *allset){

	struct file_info *current_file;
	struct sync_message response_packet;
	int response_packet_size = sizeof(response_packet);
	
	if(strlen(received_packet.filename) == 0){ 
		//Checking for empty files.
		send_new_file(sock, client);
		client->state = SYNC;

	}else{ 
		//A regular sync packet.

		if(client->sharing){ 
			/* If directory is being shared, check if this filename
			 * already exists on the server's file system. If so, update
 			 * this client's file_info array with the information of the file
 			 * from the file system.
			 */
			add_shared(client, received_packet.filename);
		}

		if((current_file = check_file(client->files, received_packet.filename)) == NULL){
			//No more files can be accepted.
			fprintf(stderr, "Maximum file limit reached for directory: %s. Non-graceful kill to client: %s\n", client->dirname, client->userid);
			close_connection(sock, client, allset);

		}else{
			//Construct and send client the respective sync_message packet.
			strncpy(response_packet.filename, current_file->filename, MAXNAME);
			response_packet.mtime = (long int)current_file->mtime;
			response_packet.size = current_file->size;

			Writen(sock, &response_packet, response_packet_size);

			if(received_packet.mtime > response_packet.mtime){
				//Client has a more recent file.
				client->state = GETFILE;

				/* Client will now send this file in CHUNKSIZE chunks. Save the name,
				 * size, the modified time of the file and number bytes that been 
				 * received and written in the client's information to keep
				 * track of which file is being expected to be read and how many
				 * more bytes are yet to be written. Update the last modified time
				 * of this file on the filesystem to timestamp as well.
				 */
				strncpy(client->get_filename, current_file->filename, MAXNAME);
				client->get_filename_readcount = 0;
				client->get_filename_size = received_packet.size;
				client->get_filename_timestamp = received_packet.mtime;

				//Update the client's file_info for this file.
				current_file->size = received_packet.size;
				current_file->mtime = (time_t)received_packet.mtime;
			
				printf("\tTX: GETFILE: %s into directory: %s, from user: %s\n", current_file->filename, client->dirname, client->userid);

			}else if(received_packet.mtime < response_packet.mtime){
				//Server has a more recent file, send it.
				printf("\tTX: SENDFILE: %s from directory: %s, to user: %s\n", current_file->filename, client->dirname, client->userid);
				send_file(sock, client->dirname, current_file->filename);
				printf("\t\tTX: Complete.\n");
 				client->state = SYNC;
			}
		}	
	}					
}

/* Check if there is any new file on the server that is not present on the
 * the client 'client' connected to by a socket 'sock'. If so send at most
 * one new file to the client by first sending the associated sync_message
 * for this file and then writing out the file. If there are no more new files
 * left on the server then send an empty sync_message to notify the client that
 * no more new files exist.
 * @Param: client the associated client_info struct for the client.
 * @Param: sock the socket at which the client is connected.
 * @Return: 1 if a new file has been sent, 0 if there were no more new files 
			(these return values are arbitary are not presently used anywhere).
 */ 
int send_new_file(int sock, struct client_info *client){

	char dirpath[CHUNKSIZE];		
	char fullpath[CHUNKSIZE];
	DIR *dir;
	struct dirent *file;
	struct stat st;
	int j, found;
	struct sync_message response_packet;
	int packet_size = sizeof(response_packet);
	struct file_info* current_file;

	//Get the relative path to this client's directory.
	strncpy(dirpath, "server_files/", 14);
	strncat(dirpath, client->dirname, CHUNKSIZE-14);

	if((dir = opendir(dirpath)) == NULL){
			perror("Opening directory: ");
			exit(1);
	}

	while(((file = readdir(dir)) != NULL)){

		/* This flag determines if the current file in this loop is new and
		 * has to be sent to the client.
		 */
		found = 1;

		strncpy(fullpath, dirpath, 256);
		strcat(fullpath, "/");
		strncat(fullpath, file->d_name, CHUNKSIZE-strlen(fullpath)); //error?

		if(stat(fullpath, &st) != 0){
			perror("stat");
			exit(1);
		}
		
		//Check if a regular file (Skip dot files and subdirectories).
		if(S_ISREG(st.st_mode)){
			
			for(j=0; j<MAXFILES; j++){

				if(client->files[j].filename[0] == '\0'){
					/* No more files left in the client to check, break out to improve run-time.
					 * It is trivially new for the client in this case, hence send it.
					 */
					found = 1;
					break;
				} 

				if(strcmp(client->files[j].filename, file->d_name) == 0){
					//Found some file which exists already exists, skip.
					found = 0;
					break;
				}
			}

			if(found){
				//The file 'file-d_name' at this iteration needs to be sent to the client.

				//Generate and send the approriate sync_message with the file information.
				strncpy(response_packet.filename, file->d_name, MAXNAME);
				response_packet.mtime = (long int)st.st_mtime;
				response_packet.size = (int)st.st_size;
				
				Writen(sock, &response_packet, packet_size);

				//Add the associated modified time, size and the filename itself to the client.
				current_file = check_file(client->files, file->d_name);
				current_file->mtime = (time_t)st.st_mtime;
				current_file->size = (int)st.st_size;

				/* Send the file to the client. Once the file is sent the functions returns an arbitary
				 * value; since at most only one new file can be transferred. If there were more new files
				 * they will be found out and successively sent at the next empty sync_messages the client
				 * sends.
				 */
				printf("\tNEWFILE TX: %s does not exist on client %s; sending. \n", file->d_name, client->userid);
				send_file(sock, client->dirname, file->d_name);
				printf("\t\tNEWFILE TX: Complete.\n");
				return 1;
			}

		}
		
	}	

		/* If this scope is reached by the function, it indicates that there are no
		 * more new files left to be sent (since any new file would have 'returned').
		 * If so, generate and send an empty sync_message to the client to notify
		 * the client that no more new files exist.
		 */
		strncpy(response_packet.filename, "", MAXNAME);
		response_packet.mtime = 0;
		response_packet.size = 0;

		Writen(sock, &response_packet, packet_size);	

		/* This also indicates an end of a cycle (see README) for this client.
		 * At the next request the client makes, refresh the modified times
		 * of ever file in the client's file_info array that have been modified
		 * on the server's file_system.
		 */
		client->refresh = 1;
	
	if(closedir(dir) == -1){
			perror("Closing directory: ");
			exit(1);
	}
	return 0;
}

/* Check if the directory being requested to be synchronized by the client
 * 'client' has already been synchronized by some other client. If so,
 * this 'client' is sharing the directory. Update the sharing flag to
 * reflect the same. This procedure is only evaluated once after the client
 * logs in.
 * @Param: client the associated client_info struct for the client.
 * @Param: client_slot the index of this client mapping in the client's array.
 * @Return: void.
 */
void check_sharing(struct client_info *client, int client_slot){

	int i;

	for(i=0; i<MAXCLIENTS; i++){
	
		if(clients[i].userid == '\0'){
			//No more active clients beyond. Quit searching further to improve run-time.
			break;
		}

		if(i == client_slot){
			//Omit this client itself.
			continue;
		}

		if(strcmp(clients[i].dirname, client->dirname) == 0){

			/* Directory name matches for some other client already
			 * present in the clients array. Change this client's
			 * sharing flag to active.
			 */

			printf("\t\tSHARING: Detected directory %s with client: %s \n", clients[i].dirname, clients[i].userid);

			client->sharing = 1;
			break;
		}
	}
}

/* Check if file 'filename' is already present in the directory being
 * synchronized by the client 'client'. If so, add the file and its
 * size and modified time from the server's file system to this client's
 * file_info array. This procedure is only evaluated if the directory
 * being shared by the 'client' is being shared.
 * @Param: client the associated client_info struct for the client.
 * @Param: filename the file name to search for.
 * @Return: void.
 */
void add_shared(struct client_info *client, char *filename){

	char dirpath[CHUNKSIZE], fullpath[CHUNKSIZE];
	DIR *dir;
	struct dirent *file;
	struct stat st;
	struct file_info *current_file;

	//Get the relative path to this client's directory.
	strncpy(dirpath, "server_files/", 14);
	strncat(dirpath, client->dirname, CHUNKSIZE-14);

	if((dir = opendir(dirpath)) == NULL){
		perror("Opening directory: ");
		exit(1);
	}

	while(((file = readdir(dir)) != NULL)){
				
		//For every file present on the server.
		if(strcmp(file->d_name, filename) == 0){

			//The file 'filename' exists.
			strncpy(fullpath, dirpath, 256);
			strcat(fullpath, "/");
			strncat(fullpath, file->d_name, CHUNKSIZE-strlen(fullpath)); 

			if(stat(fullpath, &st) != 0){
				perror("stat");
				exit(1);
			}

			//Add the associated modified time, size and the filename itself to the client.
			current_file = check_file(client->files, file->d_name);
			current_file->mtime = (time_t)st.st_mtime;
			current_file->size = (int)st.st_size;
			break;
		}
	}

	if(closedir(dir) == -1){
		perror("Closing directory: ");
		exit(1);
	}
}

/* For every file present in the client 'client' file_info array (for every file
 * that been synchronized) check if the file has been updated on the server's
 * file system. If so, update the new modified time from the file system to the
 * client's file_info array. (Required for detecting if any files has been changed
 * on the server, and sending the same back to the client). This proecdure takes
 * place at start of every 'cycle' as documented in the README.
 * @Param: client the associated client_info struct for the client.
 * @Return: void.
 */
void refresh_file_times(struct client_info *client){
	int j;
	char dirpath[CHUNKSIZE];
	char fullpath[CHUNKSIZE];
	DIR *dir;
	struct dirent *file;
	struct stat st;

	//Get the relative path to this client's directory.
	strncpy(dirpath, "server_files/", 14);
	strncat(dirpath, client->dirname, CHUNKSIZE-14);
	
	for(j=0; j<MAXFILES; j++){
					
		if(client->files[j].filename[0] == '\0'){
			//Client has no more files, break out to improve run-time.
			break;
		}else{
			//Open the server directory for this clients and check if this file exists.
			if((dir = opendir(dirpath)) == NULL){
				perror("Opening directory: ");
				exit(1);
			}

			while(((file = readdir(dir)) != NULL)){

				if(strcmp(client->files[j].filename, file->d_name) == 0){
					//This file exists on the server (has been synchronized before).

					strncpy(fullpath, dirpath, CHUNKSIZE);
					strcat(fullpath, "/");
					strncat(fullpath, file->d_name, CHUNKSIZE-strlen(file->d_name));

					if(stat(fullpath, &st) != 0){
						perror("stat");
						exit(1);
					}
					
					/* Check if the server's filesystem has a new modiciation time
					 * for this file, if so update this client's file information
					 * array.
					 */
					if(client->files[j].mtime < st.st_mtime){
						printf("\tMODIFIED: On SERVER: %s\n", client->files[j].filename);
						client->files[j].mtime = st.st_mtime;
						client->files[j].size = (int)st.st_size;
					}

					break;
				}

			}

			if(closedir(dir) == -1){
				perror("Closing directory: ");
				exit(1);
			}
		}
	}
}

/* Retrieve a file from the client given by a file name in the client's information.
 * If no bytes have been read yet, overwite the file (if it exists) or create one. 
 * If some bytes have already written to the file and a transaction is ongoing, 
 * append to the file. Both actions write contents from a buffer 'buffer' of length 
 * 'length' and write to the file. If this sequence of write completes the writing
 * (if up to file size has been written) then upddate the client's state back to SYNC.
 * @Param: sock the socket at which the client is connected (only required to close
 * 			the connection in an event of a failure).
 * @Param: client the associated client_info struct for the client sending data.
 * @Param: buffer the buffer containting the data to be written.
 * @Param: length the length of the data in the buffer.
 * @Return: void.
 */
void get_file(int sock, struct client_info *client, char *buffer, int length){
	
	FILE *fp;
	char fullpath[CHUNKSIZE];	

	//Grab the full path to this file on the server.	
	strncpy(fullpath, "server_files/", 14);
	strncat(fullpath, client->dirname, CHUNKSIZE-14);
	strcat(fullpath, "/");
	strncat(fullpath, client->get_filename, CHUNKSIZE-strlen(fullpath));

	/* If 'read_count' for this file is not 0, then the server has already wrote
	 * some bytes to this file from the client. Is so, append any further bytes
	 * received to the file. Otherwise, this is the first time the file is being
	 * accessed to write, purge any previous contents (or create the file if it
	 * it didn't exist) and then write the bytes.
 	 */
	if(client->get_filename_readcount){
		if((fp = fopen(fullpath, "a")) == NULL) {
			perror("fopen on get file: ");
			exit(1);
    	}
	}else{
		if((fp = fopen(fullpath, "w")) == NULL) {
			perror("fopen on get file: ");
			exit(1);
    	}
	}

	//Write the contents present in 'buffer' of length 'length' to the file.
	fwrite(buffer, length, 1, fp);

	//If there was an error with fwrite.
	if(ferror(fp)){
    	fprintf(stderr, "A write error occured.\n");
		Close(sock);
		exit(1);
	}

	//Update how many bytes have been read for this particular file.
	client->get_filename_readcount += length;

	/* Check if all bytes have been recieved and written to the file. If so,
	 * change this client's state to SYNC state.
	 */
	if(client->get_filename_readcount == client->get_filename_size){
		printf("\t\tCOMPLETE TX: %s into directory: %s, from user: %s\n", client->get_filename, client->dirname, client->userid);
		client->state = SYNC;
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
	new_times.modtime = (time_t)client->get_filename_timestamp;
	
	if(utime(fullpath, &new_times) < 0) {
    	perror("utime");
    	exit(1);
    }
}

/* Send a file 'filename' present in the directory 'directory' (at the 
 * predefined server path) by reading it in CHUNKSIZE parts and writing 
 * to the socket 'soc'.
 * @Param: soc the socket to write the file to.
 * @Param: directory the directory where the file is present (relative
 * 			to the path where server stores the client files)
 * @Param: filename the file to send.
 * @Return: void.
 */
void send_file(int sock, char *directory, char *filename){
	
	FILE *fp;
	char fullpath[CHUNKSIZE];	
	char buffer[CHUNKSIZE];
	int bufsize = CHUNKSIZE;
	int i;

	//Grab the full path to the file on the server.
	strncpy(fullpath, "server_files/", 14);
	strncat(fullpath, directory, CHUNKSIZE-14);
	strcat(fullpath, "/");
	strncat(fullpath, filename, CHUNKSIZE-strlen(fullpath));

	if((fp = fopen(fullpath, "r")) == NULL) {
		perror("fopen on send file: ");
		exit(1);
    }

	//Read up to bufsize or EOF (whichever occurs first) from 'fp'.
	while((i = fread(buffer, 1, bufsize, fp))){
		if(ferror(fp)){
    		fprintf(stderr, "A read error occured.\n");
			Close(sock);
			exit(1);
		}

		//Write the 'i' bytes read from the file to the socket 'soc'.	
		Writen(sock, &buffer, i);
	}

	if((fclose(fp))) {
		perror("fclose: ");
		exit(1);
    }
}

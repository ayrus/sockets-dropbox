sockets-dropbox
===============

About
-----

C implmentation of a simple Dropbox like system with client and server modules 
that synchronize files both-ways and enable sharing of directories. This was
completed as a part of my second year coursework (Spring 2012) for an
[assignment](http://www.cdf.toronto.edu/~csc209h/winter/posted_assignments/a4.shtml).


Usage
-----

Compile the source using the Makefile provided.

Server Usage: Execute dbserver.
Client Usage: dbclient -h server_hostname -d directory_to_sync -u username


Info
----

###Working:

 * Server stores synchronized directories in a directory called 
	"server_files". [Check assumption #3].

 * When the server is first run, it sets up the connection and listens
	for an incoming client request.

 * When a client connects to the server, it first sends the login message.

 * The server parses the login message, adds the client information and
	check if the directory being requested by the client to be synchronized
	has been previously synchronized by any other client. If so, a flag for
	synchronization for this client is enabled.

 * Once logged in, the server expects a sync\_message and processes the
   request accordingly.

  * If an incoming sync\_message is originating for a client who has 
  its sharing flag set to true, then for every file sent in a sync\_message
  packet by this client the server first checks to see if the file is
  present in the directory being shared, if so its name, size and last
  modified time are populated in this client's file\_info array to enable
  effective shared synchronization.

  * Once the server is in a state to send an empty sync\_message to a
  client as well; this is said to be "completion of a CYCLE". After
  completion of a cycle, the refresh flag for a particular client (who
  has completed the cycle on the server) is set to true. Once a new 
  sync\_message arrives from the client beyond this point, all files in 
  this client's file_info array are checked and compared with the files
  present in the server's sharing file system to see if any of these files
  have been modified on the server (using the modification times). This is
  done so that any files which have been changed on the server (say, by
  editing it or by some other client changing it possibly due to sharing)
  gets updated throughout.

###Assumptions:
 1. The directory given by the client is present in the current working 
	directory from which dbclient is called.
 
 2. The relative path to a file on the server or client never exceeds 255 in 
	length. Eg: 'server\_files/client_dir/somefile' < 255.

 3. A server, if rebooted is assumed to take no notice of files already
	present in its sharing file system from previous executions; upon booting. 
	Hence the folders are presumed to be empty.

/*
   Written by Matias Fernandez <matias.fernandez@gmail.com>
   Copyright 2004

   Distributed under the GPL License, for more information
   see the COPYING file
   
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <unistd.h>

#define MAXCLIENTS 255
#define VERSION "0.2"

struct client_data_t {
	int sd;
};

struct clients_t {
	int sd;
	int id;
	char nick[255];
	int challenger;
	int otherside;
	int connected;
};

struct clients_t clients[MAXCLIENTS];


/* function prototypes */
int main();
void usage(char*);
void fast_nanosleep(int, long);
void *server_listen(void *);
void *server_connection(void *);
void sendtext(int, char*);
void send_play_req(char *, int);
void direct_connect(int, int);
void direct_connect2(int);
char *strupr(char*);

/* global variables */
int defport = 7845;
int _fork = 1;
int _be_quiet = 0;
char listen_addr[255];

int main(int argc, char *argv[]) {
	
	/* default listen address */
	strcpy(listen_addr, "0.0.0.0");
	
	/* parse command line options */
	int carg = -1;
	opterr = 0;
	while ((carg = getopt(argc, argv, "p:da:q")) != -1) {
		switch (carg) {
			case 'p':	/* listen on specified port */
				defport = atoi(optarg);
				if (defport < 1) {
					fprintf(stderr, "Invalid port!\n");
					exit(1);
				}
				break;
			case 'd':
				/* do not fork */
				_fork = 0;
				break;
			case 'a':
				/* listen on spcecified address */
				strcpy(listen_addr, optarg);
				break;
			case 'q':
				/* be quiet (don't print anything) */
				_be_quiet = 1;
				break;
			case '?':
				usage(argv[0]);
				break;
		}
	}
	
		
	/* fork into the background */
	pid_t pid;
	if (_fork == 1) {
		pid = fork();
		if(pid > 0) exit(0);
		if(pid < 0) {
			fprintf(stderr, "Error creating child process!\n");
			exit(1);
		}
	}
	
	/* be quiet? */
	if (_be_quiet == 1) {
		FILE *devnull;
		devnull = fopen("/dev/null", "w");
		fclose(stderr);
		fclose(stdout);
		stderr = devnull;
		stdout = devnull;
	}
	
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_t thread;
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	if(pthread_create(&thread, &attr, &server_listen, NULL)!=0) {
		fprintf(stderr, "Unable to create server_listen thread!\n");
		exit(1);
	}
	
	printf("Running...\n");
	for(;;) {
		fast_nanosleep(1,0);
	}
}

void usage(char *binname) {
	printf("Zsnesd %s - Usage:\n", VERSION);
	printf("%s -p <port_number> -d -a <ip_addr>\n\n", binname);
	printf("-p <port_number>\tListen on specified port\n");
	printf("                \tDefault: 7845\n\n");
	printf("-d              \tDon't fork into background.\n\n");
	printf("-a <ip_addr>    \tListen on specified IP Address\n");
	printf("                \tDefault: 0.0.0.0\n\n");
	printf("-q              \tBe quiet, don't print anything to the console.\n");
	exit(1);
}

void fast_nanosleep(int secs, long nsecs) {
        /* fast and clean way to use nanosleep */
        struct timespec sleeptimer;
        sleeptimer.tv_sec=secs;
        sleeptimer.tv_nsec=nsecs;
        nanosleep(&sleeptimer, NULL);
}

void *server_listen(void *args) {
	struct sockaddr_in server_socket;
	struct sockaddr_in client_socket;
	int sock_descriptor;
	int address_size;
	int sd;
	char buf[255];
	int val=1;
	
	memset(&server_socket,0,sizeof(server_socket));
	server_socket.sin_family=AF_INET;
	server_socket.sin_port=htons(defport);
	inet_aton(listen_addr, &server_socket.sin_addr);
	if((sock_descriptor=socket(AF_INET,SOCK_STREAM,0))==-1) {
		fprintf(stderr, "Unable to open socket!\n");
		exit(1);
	}
	setsockopt(sock_descriptor, SOL_SOCKET, SO_REUSEADDR, &val, sizeof(val));
	
	if(bind(sock_descriptor, (struct sockaddr *)&server_socket, sizeof(server_socket))==-1) {
		fprintf(stderr, "Unable to bind to port %d!\n", defport);
		exit(1);
	}
	
	if(listen(sock_descriptor,20)==-1) {
		fprintf(stderr, "Unable to listen!\n");
		exit(1);
	}
	
	printf("Listening on %d\n", defport);
	address_size=sizeof(client_socket);
	
	// values for threads
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_t thread;
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	
	struct client_data_t client_data;
	bzero(buf, sizeof(buf));
	// wait and accept for an incoming connection
	for (;;) {
		sd=accept(sock_descriptor, (struct sockaddr *)&client_socket, &address_size);
		// and create a thread to handle it
		client_data.sd = sd;
		if(pthread_create(&thread, &attr, &server_connection, &client_data)!=0) {
			fprintf(stderr, "Unable to create server_connection thread!\n");
			exit(1);
		}
	}
	
	pthread_exit(0);
	
}

void sendtext(int sd, char *text) {
	char buffer[8096];
	sprintf(buffer, "\x02%s", text);
	send(sd, buffer, strlen(buffer), 0);
	send(sd, "\x00", 1 ,0);
}

void *server_connection(void *args) {
	char buf[8096];
	char sendbuf[8096];
	int len;
	int sd;
	int i, ii;
	int protoid=0;
	int init=0;
	char *chattext;
	char nick[255];
	int myid = -1;
	char *temp;
	
	struct client_data_t *client_data;
	client_data=(struct client_data_t*)args;
	sd = client_data->sd;
	
	
	printf("Accept!\n");
	for(;;) {
		bzero(buf, sizeof(buf));
		/* set socket as non blocking */
		fcntl(sd, F_SETFL, fcntl(sd, F_GETFL) | O_NONBLOCK | MSG_WAITALL);
		len=recv(sd, buf, 255, 0);
		
		if (len == 0) break;
		
		if (len > 0) {
			/* manage protocol */
			if (strlen(buf) > 3 && buf[0] == 'I' && buf[1] == 'D') {
				printf("ID request\n");
				send(sd, buf, strlen(buf), 0);	/* reply it with the same ID */
			}
			
			else if (strlen(buf) == 1 && buf[0] == 1) {
				printf("(1) REQUEST\n");
				bzero(sendbuf, sizeof(sendbuf));
				sprintf(sendbuf, "\x01");
				send(sd, sendbuf, strlen(sendbuf), 0);
				protoid=1;
			}
			
			/* text begins with \x02 */
			else if (buf[0] == 2) {
				printf("(chat) %s\n", buf);
				chattext = strstr(buf, ">");
				if (strcmp(chattext, ">.INIT") == 0) {
					/* initialize and register the client */
					/* try to figure out what is our nick... */
					if (init == 1) {
						sendtext(sd, "CAN'T INIT TWICE!");
						goto endinit;
					}
					for (i=0; i<=strlen(buf); i++) {
						if (buf[i] == '>')
							break;
					}
					
					if (i==strlen(buf)) {
						sendtext(sd, "UNABLE TO FIND YOUR NICK!");
						break;
					} else {
						bzero(nick, sizeof(nick));
						for (ii=0; ii<i ;ii++) {
							nick[ii-1] = buf[ii];
						}
						//nick[ii]=0;
					}
					
					/* check if the nick is already registered */
					for (i=0; i<=MAXCLIENTS; i++) {
						if (strcmp(clients[i].nick, nick) == 0) {
							sendtext(sd, "YOUR NICK IS ALREADY ONLINE");
							sendtext(sd, "PLEASE CHOOSE ANOTHER.");
							fast_nanosleep(3,0);
							goto endthread;
						}
					}
					
					// register client
					for (i=0; i<=MAXCLIENTS; i++) {
						if (clients[i].sd == 0) {	/* find an empty slot */
							myid = i;
							//clients[myid].nick = (char *)malloc(sizeof(char *) * 64);
							strcpy(clients[myid].nick, "");
							sprintf(clients[myid].nick, "%s", nick);
							clients[myid].id = myid;
							sprintf(sendbuf, "REGISTERED AS '%s' ON SLOT %d.", clients[myid].nick, myid);
							sprintf(clients[myid].nick, "%s", strupr(clients[myid].nick));
							sendtext(sd, sendbuf);
							sendtext(sd, "!! IMPORTANT !!");
							sendtext(sd, "PLEASE UNCHECK ALL THE 'PLAYER SELECT' CHECK BOXES.");
							sendtext(sd, "---");
							clients[myid].sd = sd;
							clients[myid].challenger=-1;
							break;
						}
					}
					
					if (i == MAXCLIENTS) {	/* can't find an empty slot! */
						sendtext(sd, "UNABLE TO FIND AN EMPTY SLOT!!");
						sendtext(sd, "PLEASE TRY AGAIN LATER");
						fast_nanosleep(3,0);
						goto endthread2;
					}
					
					init = 1;
					sendtext(sd, "DONE!");
					endinit:
					init = init;	/* this is stupid, but gcc complains if nothing is to do
							   after the label */
				}
				
				/* .WHO request */
				if (strcmp(chattext, ">.WHO") == 0 && init == 1) {
					for (i = 0 ; i <= MAXCLIENTS ; i++) {
						if (clients[i].sd != 0) {
							sprintf(sendbuf, "%d: %s", clients[i].id, clients[i].nick);
							sendtext(sd, sendbuf);
						}
					}
					sendtext(sd, "--");
				}
				
				/* play request */
				if (strncmp(chattext, ">.PLAY ", 7) == 0 && strlen(chattext) > 7 && init == 1) {
					temp = (char *)malloc(sizeof(char *) * 255);
					sscanf(chattext, ">.PLAY %s", temp);
					sprintf(sendbuf, "SENDING REQUEST TO %s", temp);
					sendtext(sd, sendbuf);
					send_play_req(temp, myid);
					free(temp);
				}
				
				/* .ACCEPT request */
				if (strcmp(chattext, ">.ACCEPT") == 0 && init == 1) {
					if (clients[myid].challenger == -1) {
						sendtext(sd, "YOU HAVE NO CHALLENGER!");
					} else {
						direct_connect(myid, clients[myid].challenger);
						goto endthread;
					}
				}
				
				
				if (init == 0) {
					sendtext(sd, "TYPE '.INIT' TO INITIALIZE YOUR CLIENT.");
				} else {
				   printf("chattext: %s\n", chattext);
				   if (strcmp(chattext, ">.HELP") == 0) {
				   	sendtext(sd, "HELP IS NOT IMPLEMENTED YET!");
					sendtext(sd, "SORRY.");
				   }
				}
				
			}
			
			else {
				printf("len: %d - '%s'\n", len, buf);
				printf("ASCII decode: ");
				for (i=0;i<=strlen(buf);i++) {
					printf("%c=%i ", buf[i], buf[i]);
				}
				printf("\n");
			}
		}
		
		if (protoid == 2) {
			sendtext(sd, "YOU ARE UNINITIALIZED!");
			sendtext(sd, "TYPE '.INIT' TO INITIALIZE YOUR CLIENT.");
			protoid = 3;
		}
		
		if (protoid == 1) {
			protoid=2;
			sendtext(sd, "WELCOME TO ZSNES SERVER!");
			send(sd, "\x03", 1, 0);		/* uncheck player 1 slot */
		}
		
		/* direct connection */
		if (myid > -1) {
			if (clients[myid].connected == 1) {
				direct_connect2(myid);
				goto endthread;
			}
		}
		fast_nanosleep(0,9999);
	}
	
	endthread:
	/* unregister the client */
	for (i=0; i<=MAXCLIENTS; i++) {
		if (clients[i].id == myid) {
			bzero(clients[i].nick, sizeof(clients[i].nick));
			strcpy(clients[i].nick, "");
			clients[i].id = 0;
			clients[i].sd = 0;
			break;
		}
	}
	endthread2:
	close(sd);
	printf("Closing connection\n");
	
	pthread_exit(0);
	
}

void send_play_req(char *nick, int sid) {
	char sendbuf[8096];
	int s, d;
	char dnick[255];
	int did;
	int dsd;
	
	/* find nick of "sid" */
	for (s=0; s<=MAXCLIENTS; s++) {
		if(clients[s].sd != 0 && clients[s].id == sid) {
			break;
		}
	}
	//printf("nick del retador: %s id: %d\n", clients[s].nick, clients[s].id);
	
	/* find the destination */
	for (d=0; d<=MAXCLIENTS; d++) {
		if (clients[d].sd != 0 && strcmp(clients[d].nick, nick) == 0) {
			sprintf(dnick, "%s", clients[d].nick);
			did = clients[d].id;
			dsd = clients[d].sd;
			clients[s].otherside = did;
			sprintf(sendbuf, "'%s' WANTS TO PLAY WITH YOU, TYPE '.ACCEPT' TO CONNECT WITH HIM.", clients[s].nick);
			sendtext(dsd, sendbuf);
			clients[d].challenger = sid;
			break;
		}
	}
}

char *strupr(char* s) {
	char *p = s;
	while(*p) {
		*p = toupper((int)*p);
		*p++;
	}
	return s;
}

void direct_connect(int local, int remote) {
	int len;
	void *buf;
	buf = malloc(255);
	
	if (clients[remote].challenger == local) {
		sendtext(clients[local].sd, "ERROR: remote challenger != local");
		return;
	}
	sendtext(clients[local].sd, "CONNECTED!");
	sendtext(clients[remote].sd, "CONNECTED!");
	clients[remote].connected = 1;
	
	for (;;) {
		/* receive from local side and redirect it to remote side */
		bzero(buf, sizeof(buf));
		len=recv(clients[local].sd, buf, 255, 0);
		if (len == 0) {	/* broken socket! */
			printf("broken socket!\n");
			break;
		}
		
		if (len > 0) {
			send(clients[remote].sd, buf, len, 0);
		}
		
		/* vice versa */
		bzero(buf, sizeof(buf));
		len=recv(clients[remote].sd, buf, 255, 0);
		if (len == 0) {	/* broken socket! */
			printf("broken socket!\n");
			break;
		}
		if (len > 0) {
			send(clients[local].sd, buf, len, 0);
		}
		fast_nanosleep(0,1000);
	}
	
	printf("end_dc!\n");
	clients[remote].connected = 0;
	free(buf);
	close(clients[remote].sd);
	close(clients[local].sd);
}

void direct_connect2(int myid) {
	for(;;) {
		if (clients[myid].connected == 0) {
			break;
		}
		fast_nanosleep(0,1000);
	}
	printf("end_dc2!\n");
}

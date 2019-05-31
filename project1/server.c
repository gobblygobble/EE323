/*****************************************************************
 *
 * SERVER CODE server.c for Assignment 1 of EE323 Computer Network
 *
 * by 20160609 Jinha Chung, School of Electrical Engineering
 *
 *****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h> // for strcpy()
#include <unistd.h> // for getopt()
#include <assert.h> // for assert()
// the following are libraries specified by the code on Beej's guide, p.25-27
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

# define BACKLOG 10 // how many pending connections queue will hold
# define MAXDATASIZE 1<<16 // number of bytes we can send()/recv() at once: 2^16

/* Miscellaneous functions... ones marked with "BG" are from
 * Beej's Guide to Network Programming by Brian "Beej Jorgensen" Hall */

// signal handler "BG"
void sigchld_handler(int s);
// get sockaddr, IPv4 "BG", modified to work only for IPv4
void *get_in_addr(struct sockaddr *sa);

int main(int argc, char* argv[])
{
	int opt;
	char port[6]; // maximum port 65536 
	int flag_p = 0;

	int sockfd, new_fd; // listen on sock_fd and make new connections on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes = 1;
	char s[INET_ADDRSTRLEN]; // change to INET6_ADDRSTRLEN if we use IPv6 as well
	int rv;

	int numbytes;
	char buf[MAXDATASIZE];

	/* 1. Parse command line arguments */
	while ((opt = getopt(argc, argv, "p:")) != -1)
	{
		switch(opt)
		{
			case 'p':
				// port will be assigned only the first numbers of optarg.
				// Ex: 100a -> 100, 100a2 -> 100, a100 -> 0
				strcpy(port, optarg);
				flag_p = 1;
				break;
			case '?':
				// getopt() will automatically raise an error.
				break;
			default:
				// if I understand getopt() correctly, it should not reach here.
				fprintf(stdout, "command line argument only takes -p and -h.\n");
				assert(0);
		}

	}
	// flag_p should be 1.
	if (flag_p != 1)
	{
		fprintf(stdout, "Please specify the option p.\n");
		fprintf(stdout, "Exiting server program now...\n");
		exit(0);
	}
	// if multiple p are given, the value will be set to the one given latest.

	/* 2. Socket, binding, and all that stuff. Adopted from "BG" */

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s.\n", gai_strerror(rv));
		return 1;
	}

	// loop through results and bind to the first one available
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}
		break;
	}
	if (p == NULL) {
		fprintf(stderr, "server: failed to bind.\n");
		return 2;
	}
	freeaddrinfo(servinfo); // done using this structure

	if (listen(sockfd, BACKLOG) == -1) {
		perror("listen");
		exit(1);
	}

	sa.sa_handler = sigchld_handler; // reap all dead processes
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	if (sigaction(SIGCHLD, &sa, NULL) == -1) {
		perror("sigaction");
		exit(1);
	}

	/* 3. Main accept() loop with internal loop for send() and recv() */
	while (1)
	{
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);

		int new = 1;

		if (!fork()) {
			// child process
			close(sockfd); // child does not need to listen()
			while (1)
			{
				numbytes = recv(new_fd, buf, MAXDATASIZE, 0);
				if (numbytes == -1) perror("recv");
				if (numbytes == 0) break; // recv returning 0 means client closed connection
				buf[numbytes] = '\0';
				
				// ready -> go to next line.
				if (!new) {
					fputc('\n', stdout);
				}
				if (new) new = 0;
				
				for (int i = 0; i < numbytes; i++) {
					fputc(buf[i], stdout);
				}
				fflush(stdout);
				//fprintf(stdout, "client: received '%s'\n", buf);
				if (send(new_fd, "server's reply", 14, 0) == -1) perror("send");
				//fprintf(stdout, "client: replied to client.\n");
			}
			close(new_fd);
			exit(0);
		}
		close(new_fd); // parent should also close this.
	}
	return 0;
}

// signal handler "BG"
void sigchld_handler(int s)
{
	while (waitpid(-1, NULL, WNOHANG) > 0) ;
}

// get sockaddr, IPv4 "BG", modified to work only for IPv4
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) return &(((struct sockaddr_in *)sa)->sin_addr);
	// IPv4 should NOT REACH HERE
	assert(0);
	return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}


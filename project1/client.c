/*****************************************************************
 *
 * CLIENT CODE client.c for Assignment 1 of EE323 Computer Network
 *
 * by 20160609 Jinha Chung, School of Electrical Engineering
 *
 *****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h> // for strcpy()
#include <unistd.h> // for getopt()
#include <assert.h> // for assert()
// the following are libraries specified by the code on Beej's guide, p.27-29
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

#define MAXDATASIZE 1<<16 // number of bytes we can send()/recv() at once: 2^16

/* Miscellaneous functions... ones marked with "BG" are from
 * Beej's Guide to Network Programming by Brian "Beej Jorgensen" Hall */

// get sockaddr, IPv4 "BG", modified to work only for IPv4
void *get_in_addr(struct sockaddr *sa);

int main(int argc, char* argv[])
{
	int opt;
	char port[6]; // maximum port 65536
	//char server_ip[16]; // 255.255.255.255 is maximum 15 characters + NULL = 16 characters
	char server_ip[INET_ADDRSTRLEN];
	int flag_p = 0; // whether p option is received
	int flag_h = 0; // whether h option is received
	int ready = 0; // whether 'ENTER' was entered last time
	char *check;
	size_t len;

	int sockfd, numbytes;
	char buf[MAXDATASIZE + 1];
	struct addrinfo hints, *servinfo, *p;
	int rv;
	//char s[INET_ADDRSTRLEN]; // change to INET6_ADDRSTRLEN if we use IPv6 as well
	
	char *ret_fgets; // return value of fgets, used in 3.
	/* 1. Parse command line arguments */
	while ((opt = getopt(argc, argv, "p:h:")) != -1)
	{
		switch(opt)
		{
			case 'p':
				// port will be assigned only the first numbers of optarg.
				// Ex: 100a -> 100, 100a2 -> 100, a100 -> 0
				strcpy(port, optarg);
				flag_p = 1;
				break;
			case 'h':
				// for safety, only copy up to 16 characters.
				strcpy(server_ip, optarg);
				flag_h = 1;
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
	// flag_p and flag_h should both be 1.
	if ((flag_p != 1) || (flag_h != 1))
	{
		fprintf(stdout, "Please specify both option p and option h.\n");
		fprintf(stdout, "Exiting client program now...\n");
		exit(0);
	}
	// if multiple p or h are given, the values will be set to the one given latest.

	/* 2. Socket generation and all that stuff. Adopted from "BG" */
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(server_ip, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s.\n", gai_strerror(rv));
		return 1;
	}

	// loop through results and connect to the first one available
	for (p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			perror("client: socket");
			continue;
		}

		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("client: connect");
			continue;
		}
		break;
	}
	if (p == NULL) {
		fprintf(stderr, "client: failed to connect.\n");
		return 2;
	}
	// this line has been commented out because server_ip is already a form of IP address.
	//inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), server_ip, sizeof server_ip);

	freeaddrinfo(servinfo); // done using this structure

	/* 3. Actual send() and recv() with server. */
	
	int c;
	int count = 0;
	int ready_to_exit = 0;
	while (1)
	{
		c = fgetc(stdin);
		count++;
		if (feof(stdin)) {
			// EOF
			if (count == 1) {
				// empty EOF
				close(sockfd);
				return 0;
			}
			else {
				// nonempty EOF
				if (send(sockfd, buf, count, 1) == -1) perror("send");
				// now wait for server to reply.
				numbytes = recv(sockfd, buf, MAXDATASIZE, 0);
				if (numbytes == -1) perror("recv");
				if (numbytes == 0) fprintf(stdout, "client: server closed connection. Ending program...\n");
				close(sockfd);
				return 0;
			}
		}
		else if (c == '\n') {
			// newline
			if (count == 1) {
				// empty line
				if (ready_to_exit) {
					// two ENTERs
					close(sockfd);
					return 0;
				}
				else {
					// one ENTER
					ready_to_exit = 1;
					count = 0;
				}
			}
			else {
				// nonempty line
				//buf[count - 1] = c;
				if (send(sockfd, buf, count, 1) == -1) perror("send");
				//if (send(sockfd, buf, count + 1, 1) == -1) perror("send");
				// now wait for server to reply.
				numbytes = recv(sockfd, buf, MAXDATASIZE, 0);
				if (numbytes == -1) perror("recv");
				if (numbytes == 0) {
					fprintf(stdout, "client: server closed connection. Ending program...\n");
					close(sockfd);
					return 0;
				}
				ready_to_exit = 1;
				count = 0;
			}
		}
		else if (count >= MAXDATASIZE) {
			// buffer full
			buf[count - 1] = c;
			if (send(sockfd, buf, count, 1) == -1) perror("send");
			// now wait for server to reply.
			numbytes = recv(sockfd, buf, MAXDATASIZE, 0);
			if (numbytes == -1) perror("recv");
			if (numbytes == 0) {
				fprintf(stdout, "client: server closed connection. Ending program...\n");
				close(sockfd);
				return 0;
			}
			ready_to_exit = 0;
			count = 0;
		}
		else {
			// nothing special
			buf[count - 1] = c;
			ready_to_exit = 0;
		}
	}
	// should not ever reach here.
	assert(0);
	close(sockfd);
	return 0;
}

// get sockaddr, IPv4 "BG", modified to work only for IPv4
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) return &(((struct sockaddr_in *)sa)->sin_addr);
	// IPv4 should NOT REACH HERE
	assert(0);
	return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

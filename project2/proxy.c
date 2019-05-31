/*****************************************************************
 *
 * PROXY CODE proxy.c for Assignment 2 of EE323 Computer Network
 *
 * by 20160609 Jinha Chung, School of Electrical Engineering KAIST
 *
 *****************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h> // for strcpy()
#include <assert.h> // for assert()
#include <unistd.h> // for close()
// the following are libraries specified by the code on Beej's guide, p.25-27
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include "proxy.h"

# define BACKLOG 10 // how many pending connections queue will hold

int main(int argc, char* argv[])
{
	int portn;
	char* endptr; // for strtol()
	char port[6]; // maximum port 65536 

	int sockfd, new_fd; // listen on sock_fd and make new connections on new_fd
	struct addrinfo hints, *servinfo, *p;
	struct sockaddr_storage their_addr; // connector's address information
	socklen_t sin_size;
	struct sigaction sa;
	int yes = 1;
	char s[INET_ADDRSTRLEN];
	int rv;

	int client_numbytes;
	int server_numbytes;
	char client_buf[MAXDATASIZE];
	char server_buf[MAXDATASIZE];

	char blacklist[BUFFER_SIZE];
	memset(blacklist, 0, BUFFER_SIZE);

	if (!isatty(STDIN_FILENO)) {
		// redirected file exists
		char c;
		int count = 0;
		while (1)
		{
			c = fgetc(stdin);
			if (feof(stdin)) break;
			blacklist[count] = c;
			count++;
		}
	}

	/* 1. Check command line's port number */
	if (argc > 2) {
		fprintf(stderr, "Too many arguments given.\n");
		return 1;
	}
	portn = (int) strtol(argv[1], &endptr, 10);
	if (*endptr != 0) {
		fprintf(stderr, "Port number should only include numbers.\n");
		return 1;
	}
	sprintf(port, "%d", portn);
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
			perror("proxy: socket");
			continue;
		}

		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
			perror("proxy: setsockopt");
			exit(1);
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("proxy: bind");
			continue;
		}
		break;
	}
	if (p == NULL) {
		fprintf(stderr, "proxy: failed to bind.\n");
		error_message("proxy: failed to bind.");
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
	/* 3. Main accept() loop with internal loop for send() and recv()
	 * and another loop inside for communication with the actual server. */
	while (1)
	{
		sin_size = sizeof their_addr;
		new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
		if (new_fd == -1) {
			perror("accept");
			continue;
		}

		inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s);
		if (!fork()) {
			// child process
			close(sockfd); // child does not need to listen()
			while (1)
			{
				// receive request from client
				int dont_send = 1;
				int client_length = 0;
				int host_in_blacklist = 0;
				char temp_client_buffer[BUFFER_SIZE];
				while (dont_send) {
					client_numbytes = recv(new_fd, temp_client_buffer, MAXDATASIZE, 0);
					if (client_numbytes == -1) {
						perror("recv");
						return 1;
					}
					if (client_numbytes == 0) return 1; // recv returning 0 means client closed connection
					if (client_numbytes > 0) {
						strncpy(client_buf + client_length, temp_client_buffer, (size_t)client_numbytes);
						client_length += client_numbytes;
					}
					if (strstr(client_buf, "\r\n\r\n") != NULL) dont_send = 0;
				}
				client_buf[client_length] = '\0';
				//fprintf(stdout, "step 1: client to proxy: %s\n", client_buf);
/* ----------------------------- parsing request message start ---------------------------- */
				RequestMessage RM;
				strncpy(RM.request_buffer, client_buf, (size_t)client_length);
				if (CheckCorrectRequestFormatFirstLine(&RM) == 0) {
					send(new_fd, "400 Bad Request", (size_t)16, 0);
					return 1;
				}
				// in this assignment we deal with only GET and HTTP/1.0
				if (RM.method != get) {
					send(new_fd, "400 Bad Request", (size_t)16, 0);
					error_message("We expect a GET method message.");
					return 1;
				}
				if (RM.version != http10) {
					send(new_fd, "400 Bad Request", (size_t)16, 0);
					error_message("We expect HTTP version 1.1.");
					return 1;
				}
				if (strstr(RM.request_buffer, "Host: ") == NULL) {
					send(new_fd, "400 Bad Request", (size_t)16, 0);
					return 1;
				}
				if (ParseURL(&RM) == 0) {
					send(new_fd, "400 Bad Request", (size_t)16, 0);
					return 1;
				}
				// check for blacklist
				if (strstr(blacklist, RM.host) != NULL) host_in_blacklist = 1;
				
				if (host_in_blacklist) {
					char warning[BUFFER_SIZE];
					// choose message
					//strncpy(warning, "GET http://warning.or.kr HTTP/1.0\r\n\r\n", (size_t)BUFFER_SIZE);
					//strncpy(warning, "GET / HTTP/1.0\r\nHost: warning.or.kr\r\nConnection: close\r\n\r\n", (size_t)BUFFER_SIZE);
					strncpy(warning, "GET / HTTP/1.0\r\nHost: warning.or.kr\r\n\r\n", (size_t)BUFFER_SIZE);
					size_t warning_length = strlen(warning) + 1;
					// set RM's instances accordingly
					strncpy(RM.request_buffer, warning, warning_length);
					strncpy(client_buf, warning, warning_length);
					strncpy(RM.url, "http://warning.or.kr", (size_t)21);
					strncpy(RM.host, "warning.or.kr", (size_t)14);
					memset(RM.path, 0, BUFFER_SIZE);
					RM.port = 80;
					RM.first_line_length = (int)warning_length - 1;
					// these two might be redundant
					RM.method = get;
					RM.version = http10;
				}

				// now establish connection with the server
/* ----------------------------- connection with server start ----------------------------- */
				// variables
				struct addrinfo hints_s, *serverinfo, *p_s;
				int rv_s;
				char server_port[6];
				sprintf(server_port, "%d", RM.port);
				int sockfd_s;
				// connection
				memset(&hints_s, 0, sizeof hints_s);
				hints_s.ai_family = AF_UNSPEC;
				hints_s.ai_socktype = SOCK_STREAM;

				if ((rv_s = getaddrinfo(RM.host, server_port, &hints_s, &serverinfo)) != 0) {
					fprintf(stderr, "getaddrinfo: %s.\n", gai_strerror(rv_s));
					return 1;
				}
				// loop through results and connect to the first one available
				for (p_s = serverinfo; p_s != NULL; p_s = p_s->ai_next) {
					if ((sockfd_s = socket(p_s->ai_family, p_s->ai_socktype, p_s->ai_protocol)) == -1) {
						perror("proxy-server: socket");
						continue;
					}

					if (connect(sockfd_s, p_s->ai_addr, p_s->ai_addrlen) == -1) {
						close(sockfd_s);
						perror("proxy-server: connect");
						continue;
					}
					break;
				}
				if (p_s == NULL) {
					error_message("proxy: failed to connect to server");
					return 2;
				}

				freeaddrinfo(serverinfo);

				char store[BUFFER_SIZE];
				int keep_alive = 1;
				int current_msg_length = 0;
				//fprintf(stdout, "step 2: proxy to server: %s", client_buf);
				if (send(sockfd_s, client_buf, client_length, 0) == -1) perror("send");
				while (keep_alive)
				{
					int maxdatasize = MAXDATASIZE - current_msg_length;
					server_numbytes = recv(sockfd_s, store, maxdatasize, 0);
					if (server_numbytes == -1) {
						perror("proxy-server: recv");
						keep_alive = 0;
					}
					if (server_numbytes == 0) {
						error_message("proxy: lost connection with the server.");
						keep_alive = 0;
					}
					if (server_numbytes > 0) {
						strncpy(server_buf + current_msg_length, store, (size_t)server_numbytes);
						current_msg_length += server_numbytes;
						memset(store, 0, BUFFER_SIZE);
						if (host_in_blacklist) {
							keep_alive = 0; // check correctness?
						}
					}
				}
/* ------------------------------ connection with server end ------------------------------ */
				// send response to client
				//fprintf(stdout, "step 3: server to proxy: %s", server_buf);
				if (send(new_fd, server_buf, current_msg_length, 0) == -1) perror("send");
				//fprintf(stdout, "step 4: proxy to client: %s", server_buf);
				break;
			}
			close(new_fd);
			exit(0);
		}
		close(new_fd); // parent should also close this.
	}
	return 0;
}

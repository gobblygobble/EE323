/**********************************************************************
 *
 * PROXY HEADER FILE proxy.h for Assignment 2 of EE323 Computer Network
 *
 * by 20160609 Jinha Chung, School of Electrical Engineering KAIST
 *
 *********************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
// the following are libraries specified by the code on BEEJ's guide, p.25-27
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define MAXDATASIZE (1<<20)
#define BUFFER_SIZE MAXDATASIZE

enum Method {
	get,
	post,
	head,
};
typedef enum Method Method;

enum Version {
	http10,
	http11,
};
typedef enum Version Version;

struct RequestMessage {
	char request_buffer[BUFFER_SIZE];
	char url[BUFFER_SIZE];
	char host[BUFFER_SIZE];
	char path[BUFFER_SIZE];
	int port;
	int first_line_length;
	Method method;
	Version version;
};
typedef struct RequestMessage RequestMessage;

/* Miscellaneous functions... ones marked with "BG" are from
 * Beej's Guide to Network Programming by Brian "Beej Jorgensen" Hall */

// signal handler "BG"
void sigchld_handler(int s)
{
	while (waitpid(-1, NULL, WNOHANG) > 0) ;
}

// get sockaddr, IPV4 "BG", modified to work only for IPv4
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) return &(((struct sockaddr_in *)sa)->sin_addr);
        // IPv4 should NOT REACH HERE
	assert(0);
	return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

// prints out specified error message in stdout
void error_message(char* message)
{
	// proxy should be silent all the time
	//fprintf(stderr, "%s\n", message);
	return;
	//exit(0);
}

// Raises error message and returns 0 on error
int CheckCorrectRequestFormatFirstLine(RequestMessage* RM)
{
	char url[BUFFER_SIZE];
	char buf[BUFFER_SIZE];
	char ver[BUFFER_SIZE];
	Method method;
	Version version;
	int url_start;

	// read from stdin -- in practice, we should read from client
	//if (fgets(buf, BUFFER_SIZE, stdin) == NULL) {
	//	error_message("Method empty.");
	//	return 0;
	//}
	
	// use request_buffer in practice
	strncpy(buf, RM->request_buffer, strlen(RM->request_buffer));

	// check if format is proper
	if (!((buf[0] == 'G') && (buf[1] == 'E') && (buf[2] == 'T') && (buf[3] == ' ')) &&
	    !((buf[0] == 'P') && (buf[1] == 'O') && (buf[2] == 'S') && (buf[3] == 'T') && (buf[4] == ' ')) &&
	    !((buf[0] == 'H') && (buf[1] == 'E') && (buf[2] == 'A') && (buf[3] == 'D') && (buf[4] == ' '))) {
		fprintf(stdout, "%s", buf);
		error_message("Unknown method type.");
		return 0;
	}
	// correct method format
	if (buf[0] == 'G') method = get;
	else if (buf[0] == 'P') method = post;
	else method = head;

	if (method == get) url_start = 4;
	else url_start = 5;
	
	int loc = url_start;
	int url_loc = 0;
	int ver_loc = 0;
	int parsing_url = 1;

	while ((buf[loc] != '\r') && (buf[loc] != '\0'))
	//while((buf[loc] != '\n') && (buf[loc] != '\0'))
	{
		// check if we're still parsing URL
		if ((parsing_url) && (buf[loc] == ' ')) {
			parsing_url = 0;
			loc++;
			continue;
		}
		if (parsing_url) {
			url[url_loc] = buf[loc];
			url_loc++;
		} else {
			ver[ver_loc] = buf[loc];
			ver_loc++;
		}
		loc++;
	}

	// check for correct format
	if (url_loc == 0) {
		error_message("No URL input.");
		return 0;
	}
	
	if (buf[loc] != '\r') {
		error_message("No carriage return.");
		return 0;
	}
	if (buf[loc + 1] != '\n') {
		error_message("No line feed.");
		return 0;
	}
	

	// should be correct inputs
	// end url and ver
	url[url_loc] = '\0';
	ver[ver_loc] = '\0';

	if (strcmp(ver, "HTTP/1.0") == 0) version = http10;
	else if (strcmp(ver, "HTTP/1.1") == 0) version = http11;
	else {
		error_message("Invalid HTTP version.");
		return 0;
	}

	strncpy(RM->url, url, (size_t)url_loc);
	RM->method = method;
	RM->version = version;

	// calculate length of the first line
	// GET_URLLENGTH_HTTP/1.1\r\n = (url_start + url_loc + 8 + 2) characters
	RM->first_line_length = url_start + url_loc + 10;

	return 1;
}

int ParseURL(RequestMessage* RM)
{
	char temp_url[BUFFER_SIZE];
	int url_len = (int)strlen(RM->url);

	if (url_len <= 7) {
		error_message("Invalid URL.");
		return 0;
	}
	strncpy(temp_url, RM->url, (size_t)url_len);

	int port;
	int loc;
	int path_exist = 0;
	int port_exist = 0;
	int path_start;
	int port_start;
	int temp_port_loc;

	if (((temp_url[0] != 'H') && (temp_url[0] != 'h')) || ((temp_url[1] != 'T') && (temp_url[1] != 't')) ||
	    ((temp_url[2] != 'T') && (temp_url[2] != 't')) || ((temp_url[3] != 'P') && (temp_url[3] != 'p')) ||
	    (temp_url[4] != ':') || (temp_url[5] != '/') || (temp_url[6] != '/')) {
		error_message("Invalid URL.");
		return 0;
	}
	// the URL has at least the "HTTP://" part correct
	// now search for the next colon.
	for (loc = 7; loc < url_len; loc++) {
		if ((temp_url[loc] == ':') && (port_exist) == 0) {
			port_exist = 1;
			port_start = loc + 1;
		}
		else if ((temp_url[loc] == '/') && (path_exist) == 0) {
			path_exist = 1;
			path_start = loc;
		}
	}
	// first check port number
	if (port_exist) {
		port = 0;
		temp_port_loc = port_start;
		while ( (temp_port_loc < url_len) && isdigit(temp_url[temp_port_loc]) )
		{
			port = 10 * port + (int)(temp_url[temp_port_loc] - '0');
			temp_port_loc++;
		}
		if ( ((path_exist) && (temp_port_loc != path_start)) || ((path_exist == 0) && (temp_port_loc != url_len)) ) {
			// http://www.google.com:8a0/path1/path2 OR http://www.google.com:80a
			error_message("URL cannot be parsed.");
			return 0;
		}
	} else port = 80;
	RM->port = port;
	// now check for path
	if (path_exist) {
		strncpy(RM->path, temp_url + path_start, (size_t)(url_len - path_start));
	} else RM->path[0] = '\0';
	int end_host;
	if ( path_exist || port_exist ) {
		if (path_exist && port_exist) end_host = (path_start < port_start) ? path_start : (port_start - 1);
		else if (path_exist && !(port_exist)) end_host = path_start;
		else end_host = port_start - 1;
		temp_url[end_host] = '\0';
	} else end_host = url_len;

	strncpy(RM->host, temp_url + 7, (size_t)(end_host - 7));

	return 1;
}

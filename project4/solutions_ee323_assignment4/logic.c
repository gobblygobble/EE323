#include <stdio.h>
#define MIN(x,y)  ((x) <= (y) ? (x) : (y))

int main(){
	
	
	FILE * clog = fopen("client_log.txt", "rt");
	FILE * slog = fopen("server_log.txt", "rt");
	
	if(clog ==NULL)
		printf("Error: No client_log.txt\n");
	if(slog ==NULL)
		printf("Error: No server_log.txt\n");
	
	char c[10];
	int swnd;
	int rem;
	int byte;
	int expected_rem= 536;
	int line =1;
	
	
	
	while( fscanf(clog, "%s", c) != EOF) {

		fscanf(clog, "%d", &swnd);
		fscanf(clog, "%d", &rem);
		fscanf(clog, "%d", &byte);
		
		if(expected_rem != rem)
			printf("client line: %d wrong!\n", line);

		
		if ( *c =='S') 
			expected_rem = rem - byte;

		else if( *c =='R') {
			if (swnd <2144) 
				expected_rem = rem +byte +536;

			else if (swnd<3072) 
				expected_rem = rem +byte +536*536/swnd;

			else
				expected_rem = rem+byte;
		}
		line++;
	}

	
	expected_rem= 536;
	line=1;
	
	while( fscanf(slog, "%s", c) != EOF) {

		fscanf(slog, "%d", &swnd);
		fscanf(slog, "%d", &rem);
		fscanf(slog, "%d", &byte);
		
		if(expected_rem != rem)
			printf("server line: %d wrong!\n", line);

		if ( *c =='S') 
			expected_rem = rem - byte;

		else if( *c =='R') {
			if (swnd <2144) 
				expected_rem = rem +byte +MIN(536,3072);

			else if (swnd<3072) 
				expected_rem = rem +byte +MIN(536*536/swnd,3072-swnd);

			else
				expected_rem = rem+byte;
		}
	}
	
	fclose(clog);
	fclose(slog);
	
	return 0;
}

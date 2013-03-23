#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <sys/inotify.h>

//Writes (nStrings - 1) prefixes to stderr, followed by a single message string
//Replaces perror
void ShowError( int nStrings, ... );

//Wrapper for struct inotify_event
typedef struct
{
	time_t timestamp;
	
	int wd;
	
	unsigned long mask;
	unsigned long cookie;
	
	unsigned long nameLength;
	unsigned long bufferLength;
	char *name;
} Event;

//Event queue structure
#define QueueMax	1024
struct
{
	int head, tail;
	Event queue[ QueueMax ];
} equeue = { 0, 0 };

//Entry point for the program
int main( int argc, char **argv )
{
	//Check if we have any arguments
	if( argc < 2 )
	{
		//Print usage info
		fprintf( stderr,
			"Usage: %s [OPTION] [FILES]\n"
			"Catches and prints file system events for a given list of files and directories\n",
			argv[ 0 ] );
		
		//Error
		return EXIT_FAILURE;
	}
	
	//Start running inotify
	int fd = inotify_init( );
	if( fd == -1 )
	{
		ShowError( 3, argv[ 0 ], "inotify_init", strerror( errno ) );
		return EXIT_FAILURE;
	}
	
	//Empty list of watch handles
	int *wd = NULL;
	int nWd = 0;
	
	//Counter variables
	int i, j;
	
	//New value for argc after option parsing
	int dargc = argc;
	
	//Parse argv for options
	j = 1;
	for( i = 1; i < argc; i ++ )
	{
		if( argv[ i ][ 0 ] == '-' )
		{
			printf( "Parsing option: %s\n", argv[ i ] );
			dargc --;
		}
		else
		{
			argv[ j ++ ] = argv[ i ];
		}
	}
	
	//argv now should contain dargc items - the program path followed by (dargc - 1) file/directory names
	//Iterate through argv for filenames
	while( -- dargc )
	{
		printf( "Found filename: %s\n", argv[ dargc ] );
	}
	
	//Success
	return EXIT_SUCCESS;
}

void ShowError( int nStrings, ... )
{
	va_list ap;
	va_start( ap, nStrings );
	
	fputs( va_arg( ap, char * ), stderr );
	while( -- nStrings )
	{
		fputs( ": ", stderr );
		fputs( va_arg( ap, char * ), stderr );
	}
	
	fputc( '\n', stderr );
	
	va_end( ap );
}

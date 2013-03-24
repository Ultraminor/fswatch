#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/inotify.h>

//New watch handles are allocated in blocks of WatchStride
#define WatchStride	256

//Writes (nStrings - 1) prefixes to stderr, followed by a single message string
//Replaces perror
void ShowError( int nStrings, ... );

//Trims trailing slashes from a null-terminated filename
inline void TrimSlashes( char *str );

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

//Handle for watch descriptor
typedef struct
{
	int wd;
	char *ath;
} Watch;

//Some option stuff
struct
{
	//Argument triggers - long- and short- forms for the option
	//Example:
	//-A and --all-events -> 'A' and "all-events" - no leading dashes
	char character;
	char *string;
	
	//Description to be printed by Usage
	char *description;
} options[ ] = {
	{ 'A', "all-events", "Catch all possible filesystem events" },
	{ 'a', "access", "Catch filesystem access events" },
	{ 'C', "create", "Catch file creation events" },
	{ 'c', "close", "Catch filesystem close events" },
	{ 'D', "delete", "Catch file deletion events" },
	{ 'd', "delete", "Catch changes to a file's metadata" },
	{ 'k', "kernel", "Catch kernel filesystem events" },
	{ 'l', "watch-link", "Don't follow symlinks (if applicable) - instead, watch the link itself" },
	{ 'L', "watch-and-follow", "Follow symlinks, but also watch the link itself" },
	{ 'm', "move", "Catch file movement events to/from a given path" },
	{ 'o', "open", "Catch file open events" },
	{ 'r', "recur", "Recursively watch child directories" },
	{ 's', "single", "Only watch for a single event" }
};

//Codes for given option
typedef enum {
	AllEvents,
	Access,
	Creation,
	Closure,
	Deletion,
	Metadata,
	Kernel,
	WatchLink,
	WatchAndFollowLink,
	Movement,
	Open,
	Recur,
	SingleEvent
} OptionCode;

//Prints usage information and exits
inline void Usage( char *pname );

//Entry point for the program
int main( int argc, char **argv )
{
	//Check if we have any arguments
	if( argc < 2 )
		Usage( argv[ 0 ] );
	
	//Start running inotify
	int fd = inotify_init( );
	if( fd == -1 )
	{
		ShowError( 3, argv[ 0 ], "inotify_init", strerror( errno ) );
		return EXIT_FAILURE;
	}
	
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
	
	//Empty list of watch handles
	Watch *wd = NULL;
	int nWd = 0;
	
	//argv now should contain dargc items - the program path followed by (dargc - 1) file/directory names
	//Iterate through argv for filenames
	while( -- dargc )
	{
		//Trim trailing slashes
		TrimSlashes( argv[ dargc ] );
		
		//Stat the file
		struct stat st;
		if( stat( argv[ dargc ], &st ) == -1 )
		{
			ShowError( 3, argv[ 0 ], argv[ dargc ], strerror( errno ) );
		}
		else
		{
			
		}
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

inline void TrimSlashes( char *str )
{
	//For { length, length - 1, ..., 1 } -> i, null out str[ i - 1 ]. Break when str[ i - 1 ] is no longer a slash
	int i;
	for( i = strlen( str ); -- i >= 0 && str[ i ] == '/'; str[ i ] = 0 );
}

inline void Usage( char *pname )
{
	//Leading whitespace for each option line
	#define OptionLead	2
	
	//Total character offset of the option descriptions
	#define DescOffset	30
	
	fprintf( stderr,
"\
Usage: %s [OPTIONS] [FILE] ... [GLOBAL OPTIONS]\n\
All options are of the form '--option <file>'\n\
Trailing options are taken to apply to all listed files\n\
", pname );
	
	int i, j;
	for( i = 0; i < ( sizeof( options ) / sizeof( options[ 0 ] ) ); i ++ )
	{
		for( j = OptionLead; j --; )
			fputc( ' ', stderr );
		
		fprintf( stderr, "-%c, --%s%n", options[ i ].character, options[ i ].string, &j );
		
		for( j += OptionLead; j < DescOffset; j ++ )
			fputc( ' ', stderr );
		
		fputs( options[ i ].description, stderr );
		fputc( '\n', stderr );
	}
	
	exit( EXIT_FAILURE );
}

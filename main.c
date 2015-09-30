#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/inotify.h>

#include "notify.h"
#include "error.h"

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
	
	//Mask for inotify
	uint32_t mask;
} options[ ] = {
	{ 'A', "all-events", "Catch all possible filesystem events", IN_ALL_EVENTS | IN_UNMOUNT | IN_Q_OVERFLOW | IN_IGNORED },
	{ 'a', "access", "Catch filesystem access events", IN_ACCESS },
	{ 'C', "create", "Catch file creation events", IN_CREATE },
	{ 'c', "close", "Catch filesystem close events", IN_CLOSE },
	{ 'D', "delete", "Catch file deletion events", IN_DELETE | IN_DELETE_SELF },
	{ 'd', "metadata", "Catch changes to a file's metadata", IN_ATTRIB },
	{ 'k', "kernel", "Catch kernel filesystem events", IN_UNMOUNT | IN_Q_OVERFLOW | IN_IGNORED },
	{ 'l', "watch-link", "Don't follow symlinks (if applicable) - instead, watch the link itself", IN_DONT_FOLLOW },
	{ 'L', "watch-and-follow", "Follow symlinks, but also watch the link itself", 0 },
	{ 'm', "move", "Catch file movement events to/from a given path", IN_MOVE },
	{ 'o', "open", "Catch file open events", IN_OPEN },
	{ 'r', "recur", "Recursively watch child directories", 0 },
	{ 's', "single", "Only watch for a single event", IN_ONESHOT }
};
#define NOptions	( sizeof( options ) / sizeof( options[ 0 ] ) )
#define IsOption( str )	( ( str )[ 0 ] == '-' )

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

//Parses an option and adds it to the given context
inline void ParseOption( char *option, NotifyMask *context );

//Prints usage information and exits
inline void Usage( void );

volatile int running = 1;
void CatchQuit( int signal )
{
	running = 0;
}

//Entry point for the program
char *pname;
int main( int argc, char **argv )
{
	pname = argv[ 0 ];
	
	//Check if we have any arguments
	if( argc < 2 )
		Usage( );
	
	//Start running inotify
	NotifyContext context;
	CreateContext( &context );
	
	//Global context
	NotifyMask global = { 0, 0, 0 };
	
	//Parse global options
	while( argc > 1 && IsOption( argv[ argc - 1 ] ) )
		ParseOption( argv[ -- argc ], &global );
	
	//Active context
	NotifyMask mask = global;
	
	//Iterate through the remainder of the arguments
	int i;
	for( i = 1; i < argc; i ++ )
	{
		//Loop, adding options to the mask, until we reach something that looks like a filename
		
		if( IsOption( argv[ i ] ) )
		{
			ParseOption( argv[ i ], &mask );
		}
		else
		{
			//Trim trailing slashes
			int j;
			for( j = strlen( argv[ i ] ); j -- && argv[ i ][ j ] == '/'; );
			
			//inotify will throw errors if the mask is empty
			if( ! mask.mask )
			{
				ShowError( pname, "warning", "Not watching path", argv[ i ], "No events specified", CNULL );
			}
			else
			{
				//Add the watch
				printf( ( AddWatch( &context, argv[ i ], & mask ) ) ? "Path added successfully: %s\n" : "Failed to add path: %s\n", argv[ i ] );
				
				//Reset mask
				mask = global;
			}
		}
	}
	
	//Sort the context
	SortContext( &context );
	
	//Add signal handlers so we can exit cleanly
	signal( SIGTERM, CatchQuit );
	signal( SIGINT, CatchQuit );
	signal( SIGQUIT, CatchQuit );
	signal( SIGHUP, CatchQuit );
	
	while( running )
	{
		//Try and grab an event
		if( ! GetEvent( &context ) )
			continue;
		
		//Print timestamp
		struct tm *etime = localtime( &context.event.timestamp );
		printf( "[%.2d:%.2d:%.2d] ", etime -> tm_hour, etime -> tm_min, etime -> tm_sec );
		
		//Print event info
		if( context.event.mask & IN_ACCESS )
			fputs( "File was accessed:", stdout );
		else if( context.event.mask & IN_MODIFY )
			fputs( "File was modified:", stdout );
		else if( context.event.mask & IN_ATTRIB )
			fputs( "File attributes were modified:", stdout );
		else if( context.event.mask & IN_CLOSE )
			printf( "File was closed (was open for %sing):", context.event.mask & IN_CLOSE_WRITE ? "writ" : "read" );
		else if( context.event.mask & IN_OPEN )
			fputs( "File was opened:", stdout );
		else if( context.event.mask & ( IN_MOVED_FROM | IN_MOVE_SELF ) )
			fputs( "File was moved from", stdout );
		else if( context.event.mask & IN_MOVED_TO )
			fputs( "File was moved to", stdout );
		else if( context.event.mask & IN_CREATE )
			fputs( "File was created:", stdout );
		else if( context.event.mask & ( IN_DELETE | IN_DELETE_SELF ) )
			fputs( "File was deleted:", stdout );
		else if( context.event.mask & IN_UNMOUNT )
			fputs( "Underlying filesystem was unmounted:", stdout );
		else if( context.event.mask & IN_IGNORED )
			fputs( "File was ignored by operating system:", stdout );
		else if( context.event.mask & IN_Q_OVERFLOW )
			fputs( "Event queue was overflowed", stdout );
		
		//Print path if necessary
		if( context.event.path )
		{
			putchar( ' ' );
			fputs( context.event.path, stdout );
			if( context.event.file )
			{
				putchar( '/' );
				fputs( context.event.file, stdout );
			}
		}
		putchar( '\n' );
	}
	
	//Add cleanup code
	puts( "Quitting..." );
	
	//Success
	return EXIT_SUCCESS;
}

inline void Usage( void )
{
	//Leading whitespace for each option line
	#define OptionLead	2
	
	//Total character offset of the option descriptions
	#define DescOffset	30
	
	//Print usage header
	fprintf( stderr,
"\
Usage: %s [OPTIONS] [FILE] ... [GLOBAL OPTIONS]\n\
Options are of the form '--option <file>' unless specified otherwise\n\
Trailing options are taken to apply to all listed files\n\
", pname );
	
	//Iterate through options array
	int i, j;
	for( i = 0; i < NOptions; i ++ )
	{
		//Print some leading whitespace
		for( j = OptionLead; j --; )
			fputc( ' ', stderr );
		
		//Print short- and long-form invocations, storing the total length
		fprintf( stderr, "-%c, --%s%n", options[ i ].character, options[ i ].string, &j );
		
		//Print whitespace for alignment
		for( j += OptionLead; j < DescOffset; j ++ )
			fputc( ' ', stderr );
		
		//Print the option description
		fputs( options[ i ].description, stderr );
		fputc( '\n', stderr );
	}
	
	//Quit
	exit( EXIT_FAILURE );
}

inline void ParseOption( char *option, NotifyMask *context )
{	
	//Skip first dash
	option ++;
	
	//If it's a long-form option,
	if( *option == '-' )
	{
		option ++;
		
		//Iterate through options
		int i;
		for( i = 0; i < NOptions; i ++ )
			if( strcmp( option, options[ i ].string ) == 0 )
				break;
		
		if( i < NOptions )
		{		
			//Handle recursion and watch/follow
			if( i == Recur )
				context -> recur = 1;
			else if( i == WatchAndFollowLink )
				context -> watchAndFollow = 1;
			else
				context -> mask |= options[ i ].mask; //Add the mask
		}
		else
		{
			ShowError( pname, "unknown long-form option", option - 1 - 1, CNULL );
		}
	}
	else //If it's a short-form option
	{
		int i, j;
		
		//Iterate through the option characters
		for( i = 0; option[ i ]; i ++ )
		{
			for( j = 0; j < NOptions; j ++ )
				if( options[ j ].character == option[ i ] )
					break;
			
			if( j < NOptions )
			{				
				//Handle recursion and watch/follow
				if( j == Recur )
					context -> recur = 1;
				else if( j == WatchAndFollowLink )
					context -> watchAndFollow = 1;
				else
					context -> mask |= options[ j ].mask; //Add the mask
			}
			else
			{
				ShowError( pname, "unknown short-form option in", option - 1, CNULL );
			}
		}
	}
}

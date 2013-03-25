#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/inotify.h>

//Structure for selection of events to be captured
typedef struct
{
	//Non-inotify params
	int recur : 1;
	int watchAndFollow : 1;
	
	//inotify mask
	uint32_t mask;
} NotifyMask;

//Wrapper for struct inotify_event
typedef struct
{
	//Timestamp for retrieval
	time_t timestamp;
	
	//inotify mask
	uint32_t mask;
	
	char *path;
	char *subPath;
	char *pathTo;
} Event;

//inotify context
#define EBufferSize	2048	//Size, in characters, of the event buffer
#define WatchStride	256	//New watch handles are allocated in blocks of WatchStride
typedef struct
{
	int fd;
	
	int nWatches;
	struct WD
	{
		int wd;
		char *path;
	} *watches;
	
	int bi, bufferLength;
	char buffer[ EBufferSize ];
	
	Event event;
} NotifyContext;

//Initializes a NotifyContext
void CreateContext( NotifyContext *context );

//Populates context -> event with a new event
int GetEvent( NotifyContext *context );

//Adds a watch to the given path based on the parameters in mask
int AddWatch( NotifyContext *context, char *path, NotifyMask *mask );

//Sorts a context so that we can search the watchlist
int CompareWD( const void *x, const void *y );
void SortContext( NotifyContext *context );

//Writes strings to stderr until a NULL (casted to char *) is encountered
//The first n - 1 are taken as prefixes and printed similar to perror
#define CNULL	( ( char * ) NULL )
void ShowError( char *string, ... );

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
	
	for( ;; )
	{
		if( GetEvent( &context ) == -1 )
			continue;
		
		struct tm *etime = localtime( &context.event.timestamp );
		printf( "[%2d:%2d:%2d] ", etime -> tm_hour, etime -> tm_min, etime -> tm_sec );
		printf( "Event caught for %s/%s\n", context.event.path, context.event.subPath );
	}
	
	//Success
	return EXIT_SUCCESS;
}

void ShowError( char *string, ... )
{
	if( string )
	{
		fputs( string, stderr );
		
		//Handle varargs
		va_list ap;
		va_start( ap, string );
		
		//Print any strings in varargs
		while( ( string = va_arg( ap, char * ) ) )
		{
			//The previous string was a prefix
			fputs( ": ", stderr );
			
			//Print the current string
			fputs( string, stderr );
		}
		
		//Newline
		fputc( '\n', stderr );
		
		va_end( ap );
	}
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
Otions are of the form '--option <file>' unless specified otherwise\n\
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

//Watches a single path unrecursively
int AddSinglePath( NotifyContext *context, char *path, NotifyMask *mask );

//Watches a directory and all subdirectories - assumes that path actually IS a directory, so be careful
int AddRecursivePath( NotifyContext *context, char *path, int pathLength, NotifyMask *mask );

int AddWatch( NotifyContext *context, char *path, NotifyMask *mask )
{
	//Empty strings are taken as root
	if( ! path || ! *path )
		path = "/";
	
	struct stat st;
	if( lstat( path, &st ) == -1 )
	{
		ShowError( pname, "lstat", strerror( errno ), CNULL );
		return 0;
	}
	
	//If it's a directory and we need to recur, pass control to AddRecusrive path
	if( S_ISDIR( st.st_mode ) && mask -> recur )
	{
		return AddRecursivePath( context, path, strlen( path ), mask );
	}
	else if( S_ISLNK( st.st_mode ) && mask -> watchAndFollow )
	{
		//Watch the linked file
		if( ! AddSinglePath( context, path, mask ) )
			return 0;
		
		//Watch the link
		mask -> mask |= IN_DONT_FOLLOW;
		if( ! AddSinglePath( context, path, mask ) )
		{
			mask -> mask ^= IN_DONT_FOLLOW;
			return 1;
		}
		mask -> mask ^= IN_DONT_FOLLOW;
		
		return 2;
	}
		
	//Just watch the file
	return AddSinglePath( context, path, mask );
}

int AddSinglePath( NotifyContext *context, char *path, NotifyMask *mask )
{
	//Check if we need to reallocate
	if( context -> nWatches % WatchStride == 0 )
	{
		void *new = realloc( context -> watches, sizeof( * context -> watches ) * ( context -> nWatches + WatchStride ) );
		if( ! new )
		{
			ShowError( pname, "malloc", strerror( errno ), CNULL );
			return 0;
		}
		
		context -> watches = new;
	}
	
	context -> watches[ context -> nWatches ].path = path;
	context -> watches[ context -> nWatches ].wd = inotify_add_watch( context -> fd, path, mask -> mask );
	if( context -> watches[ context -> nWatches ].wd == -1 )
	{
		ShowError( pname, "inotify_add_watch", strerror( errno ), CNULL );
		return 0;
	}
	
	context -> nWatches ++;
	
	return 1;
}

int AddRecursivePath( NotifyContext *context, char *path, int pathLength, NotifyMask *mask )
{
	//Watch the directory - watchCount is count of succesfully added paths
	int watchCount = AddSinglePath( context, path, mask );
	
	//Get a dir handle
	DIR *dir = opendir( path ) ;
	
	//Errcheck
	if( dir == NULL )
	{
		ShowError( pname, "opendir", strerror( errno ), CNULL );
		return 0;
	}
		
	//Reset errno, since readdir has a funny error mechanism
	errno = 0;
	struct dirent *d;
	while( ( d = readdir( dir ) ) )
	{
		//Ignore references to self/parent
		if( strcmp( d -> d_name, "." ) == 0 || strcmp( d -> d_name, ".." ) == 0 )
			continue;
		
		//Length of path for subdir, including '/' but not including null byte
		int sLength = strlen( d -> d_name );
		int spLength = pathLength + sLength + 1;
		
		//Allocate buffer
		char *subPath = ( char * ) malloc( sizeof( char ) * ( spLength + 1 ) );
		if( ! subPath )
		{
			ShowError( pname, "malloc", strerror( errno ), CNULL );
			continue;
		}
			
		//Fill it with the new path
		memcpy( subPath, path, pathLength );	//Parent path
		subPath[ pathLength ] = '/';	//Slash
		memcpy( subPath + pathLength + 1, d -> d_name, sLength ); //Component name
		subPath[ spLength ] = 0;	//Null byte
		
		
		struct stat st;
		if( lstat( subPath, &st ) == -1 )
		{
			ShowError( pname, "lstat", strerror( errno ), CNULL );
			continue;
		}
		
		//If we have yet another directory, we need to call recursively
		if( S_ISDIR( st.st_mode ) )
			watchCount += AddRecursivePath( context, subPath, spLength, mask );
		
		//Again, error handling for readdir
		errno = 0;
	}
	
	if( errno ) //Error in readdir
		ShowError( pname, "readdir", strerror( errno ), CNULL );
		
	//Close the directory handle
	if( closedir( dir ) == -1 )
		ShowError( pname, "closedir", strerror( errno ), CNULL );
	
	return watchCount;
}

void CreateContext( NotifyContext *context )
{
	context -> fd = inotify_init( );
	if( context -> fd == -1 )
	{
		ShowError( pname, "inotify_init", strerror( errno ), CNULL );
		exit( EXIT_FAILURE );
	}
	
	context -> nWatches = 0;
	context -> watches = NULL;
	context -> bi = 0;
	context -> bufferLength = 0;
}

int CompareWD( const void *x, const void *y )
{
	return ( ( struct WD * ) x ) -> wd - ( ( struct WD * ) y ) -> wd;
}

inline void SortContext( NotifyContext *context )
{
	qsort( context -> watches, context -> nWatches, sizeof( *context -> watches ), CompareWD );
}

int GetEvent( NotifyContext *context )
{
	if( context -> bi >= context -> bufferLength )
	{
		context -> bi = 0;
		context -> bufferLength = read( context -> fd, context -> buffer, EBufferSize );
		if( context -> bufferLength == -1 )
		{
			ShowError( pname, read, strerror( errno ), NULL );
			context -> bufferLength = 0;
			return -1;
		}
	}
	
	struct inotify_event *e = ( struct inotify_event * )( &context -> buffer[ context -> bi ] );
	
	context -> event.timestamp = time( NULL );
	context -> event.mask = e -> mask;
	
	struct WD search;
	search.wd = e -> wd;
	struct WD *index = bsearch( &search, context -> watches, context -> nWatches, sizeof( struct WD ), CompareWD );
	
	context -> event.path = ( index ) ? index -> path : "unknown path";
	context -> event.subPath = e -> len ? e -> name : NULL;
	
	context -> bi += sizeof( struct inotify_event ) + e -> len;
	
	return 0;
}

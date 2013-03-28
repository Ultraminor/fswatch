#define __NOTIFY_C__

#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/inotify.h>

#include "notify.h"
#include "error.h"

//I don't know if it makes any sense to output to stderr from an arbitrary library function (nor to make calls to exit).
//However, I don't think such a small project merits any sort of compicated error setup -
//especially since this is just a refactoring of the original main.c.

extern char *pname;

//Comparison function for SortContext (and for bsearch in GetEvent)
int CompareWD( const void *x, const void *y );

//Watches a single path unrecursively
int AddSinglePath( NotifyContext *context, char *path, NotifyMask *mask );

//Watches a directory and all subdirectories - assumes that path actually IS a directory, so be careful
int AddRecursivePath( NotifyContext *context, char *path, int pathLength, NotifyMask *mask );

int AddWatch( NotifyContext *context, char *path, NotifyMask *mask )
{
	//Empty strings are taken as root
	if( ! path || ! *path )
		path = "/";
	
	//stat the file
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

Event *GetEvent( NotifyContext *context )
{
	if( context -> bi >= context -> bufferLength )
	{
		context -> bi = 0;
		context -> bufferLength = read( context -> fd, context -> buffer, ContextBufferLength );
		if( context -> bufferLength == -1 )
		{
			ShowError( pname, read, strerror( errno ), NULL );
			context -> bufferLength = 0;
			return NULL;
		}
	}
	
	struct inotify_event *e = ( struct inotify_event * )( &context -> buffer[ context -> bi ] );
	
	context -> event.timestamp = time( NULL );
	context -> event.mask = e -> mask;
	
	struct WD search;
	search.wd = e -> wd;
	struct WD *index = bsearch( &search, context -> watches, context -> nWatches, sizeof( struct WD ), CompareWD );
	
	context -> event.path = ( index ) ? index -> path : NULL;
	context -> event.file = e -> len ? e -> name : NULL;
	
	context -> bi += sizeof( struct inotify_event ) + e -> len;
	
	return &context -> event;
}

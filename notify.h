#ifndef __NOTIFY_H__
#define __NOTIFY_H__

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
	
	//Root path and filename for event, if applicable
	char *path;
	char *file;
} Event;

//inotify context
#define WatchStride	256	//New watch handles are allocated in blocks of WatchStride
#define ContextBufferLength	4096	//Size of context read bufer
typedef struct
{
	//inotify handle
	int fd;
	
	//Watch handles
	int nWatches;
	struct WD
	{
		int wd;
		char *path;
	} *watches;
	
	int bi;
	int bufferLength;
	char buffer[ ContextBufferLength ];
	
	//Event used by GetEvent
	Event event;
} NotifyContext;

//Initializes a NotifyContext
void CreateContext( NotifyContext *context );

//Grabs a (statically allocated) event structure
Event *GetEvent( NotifyContext *context );

//Adds a watch to the given path based on the parameters in mask
int AddWatch( NotifyContext *context, char *path, NotifyMask *mask );

//Sorts a context so that we can search the watchlist
inline void SortContext( NotifyContext *context );

#endif

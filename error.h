#ifndef __ERROR_H__
#define __ERROR_H__

//Writes strings to stderr until a NULL (casted to char *) is encountered
//The first n - 1 are taken as prefixes and printed similar to perror
#define CNULL	( ( char * ) NULL )
void ShowError( char *string, ... );

#endif

#define __ERROR_C__

#include <stdio.h>
#include <stdarg.h>

#include "error.h"

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

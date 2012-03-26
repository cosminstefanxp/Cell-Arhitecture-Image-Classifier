/*
 * Designed by Cosmin Stefan-Dobrin with help from materials created by
 * Razvan Deaconescu.
 *
 * Debugging-tools file, with useful macros that can be used in the development process.
 */

#ifndef DEBUG_H_
#define DEBUG_H_

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

/* log levels */
enum {
	LOG_EMERG = 1,
	LOG_ALERT,
	LOG_CRIT,
	LOG_ERR,
	LOG_WARNING,
	LOG_NOTICE,
	LOG_INFO,
	LOG_DEBUG,
	LOG_CRAP
};

/*
 * initialize default loglevel (for dlog)
 * may be redefined in the including code
 */

#ifndef LOG_LEVEL
#define LOG_LEVEL	LOG_CRAP
#endif

/*
 * define DEBUG macro as a compiler option:
 *    -DDEBUG for GCC
 *    /DDEBUG for MSVC
 */

#if defined DEBUG_
	#define dprintf(msg,...)  printf("[DEBUG][%s] " msg "\n", unitName, ##__VA_ARGS__)
#else
	#define dprintf(msg,...)  do {} while(0)               /* do nothing */
#endif

#if defined DEBUG_
#define dlog(level, format, ...)				\
	do {							\
		if (level <= LOG_LEVEL)				\
			dprintf(format, ##__VA_ARGS__);		\
	} while (0)
#else
#define dlog(level, format, ...)				\
	do {							\
	} while (0)
#endif

/* useful macro for handling error codes */
#define DIE(assertion, call_description)				\
	do {								\
		if (assertion) {					\
			fprintf(stderr, "[**ERROR**](%s, %d): ",			\
					__FILE__, __LINE__);		\
			perror(call_description);			\
			exit(EXIT_FAILURE);				\
		}							\
	} while(0)


/* Error macro which prints a given message to the stderr stream.*/
#define eprintf(msg,...)  fprintf(stderr,"[**ERROR**][%s] " msg, unitName, ##__VA_ARGS__)

/*Error function which prints the error message and quits the program.*/
#define errorExit(text, errorCode)    { perror(text); exit(errorCode);}



#endif

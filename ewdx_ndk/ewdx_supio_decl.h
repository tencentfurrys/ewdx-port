
//
//	ewdx_supio_decl.h - supio interface for the ewdx NDK port.
//
//	Derived from OpenHSP src/hsp3/linux/supio_linux.h (onitama/OpenHSP).
//	The HSP core is compiled WITHOUT HSPLINUX/HSPNDK so HSPUTF8 stays off
//	(the game is Shift-JIS; UTF-8 string semantics would corrupt SJIS text).
//	Without a platform macro OpenHSP's own supio.h declares nothing, so this
//	header is force-included (-include) for every hsp3core TU and implemented
//	by ewdx_supio.cpp (SJIS-aware string utils, POSIX fs, logcat Alerts).
//
//	Deviations from supio_linux.h:
//	  - utf8strlen/SecurityCheck omitted (no ungated callers in our core set;
//	    STRLEN falls back to strlen via supio.h, SecurityCheck is HSP3IMP-only).
//
#ifndef __ewdx_supio_decl_h
#define __ewdx_supio_decl_h

#include <stddef.h>

// hsp3config.h only defines these under HSPWIN/HSPGCC; the ewdx build sets
// neither (HSPUTF8 must stay off), so provide the POSIX values here.
#ifndef HSP_MAX_PATH
#define HSP_MAX_PATH 256
#endif
#ifndef HSP_PATH_SEPARATOR
#define HSP_PATH_SEPARATOR '/'
#endif

#define HSPAPICHAR char
#define HSPCHAR char

char *mem_ini( int size );
void mem_bye( void *ptr );
int mem_save( char *fname, void *mem, int msize, int seekofs );
void strcase( char *str );
char *mem_alloc( void *base, int newsize, int oldsize );

int strcpy2( char *str1, char *str2 );
int strcat2( char *str1, char *str2 );
#ifndef EWDX_SUPIO_SKIP_STRSTR2
// strnote.cpp defines its own file-static SJIS strstr2 and needs nothing
// else from this header; it compiles with -DEWDX_SUPIO_SKIP_STRSTR2.
char *strstr2( char *target, char *src );
#endif
char *strchr2( char *target, char code );
void getpath( char *stmp, char *outbuf, int p2 );
int makedir( char *name );
int changedir( char *name );
int delfile( char *name );
int dirlist( char *fname, char **target, int p3 );
int gettime( int index );
void strsp_ini( void );
int strsp_getptr( void );
int strsp_get( char *srcstr, char *dststr, char splitchr, int len );
int GetLimit( int num, int min, int max );
void CutLastChr( char *p, char code );
char *strsp_cmds( char *srcstr );
int htoi( char *str );

char *strchr3( char *target, int code, int sw, char **findptr );
void TrimCode( char *p, int code );
void TrimCodeL( char *p, int code );
void TrimCodeR( char *p, int code );

void ReplaceSetMatch( char *src, char *match, char *result, int in_src, int in_match, int in_result );
char *ReplaceStr( char *repstr );
int ReplaceDone( void );

void Alert( char *mes );
void AlertV( char *mes, int val );
void Alertf( char *format, ... );

HSPAPICHAR *chartoapichar( const HSPCHAR*,HSPAPICHAR** );
void freehac( HSPAPICHAR** );
HSPCHAR *apichartohspchar( const HSPAPICHAR*,HSPCHAR** );
void freehc( HSPCHAR** );
HSPAPICHAR *ansichartoapichar(const char *, HSPAPICHAR **);
char *apichartoansichar(const HSPAPICHAR *, char **);
void freeac(char **);

#endif

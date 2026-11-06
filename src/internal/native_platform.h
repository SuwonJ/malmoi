#ifndef MALMOI_NATIVE_PLATFORM_H
#define MALMOI_NATIVE_PLATFORM_H
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#include <io.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
typedef CRITICAL_SECTION pthread_mutex_t;
static int pthread_mutex_init(pthread_mutex_t *m, void *unused){(void)unused;InitializeCriticalSection(m);return 0;}
static int pthread_mutex_lock(pthread_mutex_t *m){EnterCriticalSection(m);return 0;}
static int pthread_mutex_unlock(pthread_mutex_t *m){LeaveCriticalSection(m);return 0;}
static int pthread_mutex_destroy(pthread_mutex_t *m){DeleteCriticalSection(m);return 0;}
static wchar_t *native_wide(const char *path){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);wchar_t *p;if(!n)return NULL;p=malloc((size_t)n*sizeof(*p));if(p&&!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,p,n)){free(p);p=NULL;}return p;}
static FILE *native_fopen(const char *path,const char *mode){wchar_t *p=native_wide(path);FILE *f=NULL;(void)mode;if(p){f=_wfopen(p,L"rb");free(p);}return f;}
#define native_stat _stat64
#define native_fstat _fstat64
#define native_fileno _fileno
#else
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define native_fopen fopen
#define native_stat stat
#define native_fstat fstat
#define native_fileno fileno
#endif
#endif

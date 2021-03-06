/* sgLog.cpp */
#define POKA 0

#define INCL_DOS
#include <os2.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include "sg.h"

extern int globalDebug;    /* from main.c */
extern int globalPid;      /* from main.c */
extern char *globalLogDir; /* from main.c */
extern struct LogFileStat *globalErrorLog;

void sgSetGlobalErrorLogFile(void)
{
  static char file[MAX_BUF];
  if(globalDebug)
    return;
  if(globalLogDir == NULL)
    strncpy(file,DEFAULT_LOGDIR,MAX_BUF);
  else
    strncpy(file,globalLogDir,MAX_BUF);
  strcat(file,"/");
  strcat(file,DEFAULT_LOGFILE);
  if(globalErrorLog)
  {  if(globalErrorLog->fd)
     {  if(strcmp(globalErrorLog->name,file))
        {   fclose(globalErrorLog->fd);
            globalErrorLog->fd = NULL;
        } else {
            return;
        }
     }
  }
  globalErrorLog = sgLogFileStat(file);
  if(globalErrorLog)
         return;

  strncpy(file,DEFAULT_LOGFILE,MAX_BUF);
  globalErrorLog = sgLogFileStat(file);

}

void sgLog(struct LogFileStat *log, char *format, ...)
{
  FILE *fd;
  char *date = NULL;
  char msg[MAX_BUF];
  va_list ap;
  va_start(ap, format);
  if(vsprintf(msg, format, ap) > (MAX_BUF - 1))
    fprintf(stderr,"overflow in vsprintf (sgLog): %s",strerror(errno));
  va_end(ap);
 {
    struct tm *newtime;
    time_t ltime;
    /* Get the time in seconds */
    time(&ltime);
    /* Convert it to the structure tm */
    newtime = localtime(&ltime);
    date = asctime(newtime);
    date[24] = 0;
 }

  if(globalDebug || log == NULL) {
    fprintf(stderr, "%s [%d] %s\n", date,globalPid, msg);
    fflush(stderr);
  } else {
    fd = log->fd;
    if(fd == NULL)
    {  int i;
       fd = fopen(log->name,"a");
       if(!fd)
          for(i=0;i<8;i++)
          {   if(errno == EISOPEN)
              {  DosSleep(1);
                 fd = fopen(log->name, "a");
                 if(fd) break;
              }
          }
    }
    if(fd == NULL){
      globalDebug = 1;
      fprintf(stderr,"%s [%d] filedescriptor closed for  %s\n",
             date,globalPid ,log->name);
      fprintf(stderr, "%s [%d] %s\n", date, globalPid, msg);
    } else {
      fprintf(fd, "%s [%d] %s\n", date, globalPid, msg);
//      fflush(fd);
      fclose(fd);
      log->fd = NULL;
    }
  }
}

void sgLogError(char *format, ...)
{
  char msg[MAX_BUF];
  va_list ap;
  va_start(ap, format);
  if(vsprintf(msg, format, ap) > (MAX_BUF - 1))
    sgLogFatalError("overflow in vsprintf (sgLogError): %s",strerror(errno));
  va_end(ap);
  sgLog(globalErrorLog,"%s",msg);
}

void sgLogFatalError(char *format, ...)
{
  char msg[MAX_BUF];
  va_list ap;
  va_start(ap, format);
  if(vsprintf(msg, format, ap) > (MAX_BUF - 1))
    return;
  va_end(ap);
}


#include <sys\types.h>
#include <sys\stat.h>

struct LogFileStat *sgLogFileStat(char *file)
{
  struct LogFileStat *sg;
  struct stat s;
  char buf[MAX_BUF];
  FILE *fd;
  int i;
  strncpy(buf,file,MAX_BUF);
  if((fd = fopen(buf, "a")) == NULL)
  {  for(i=0;i<8;i++)
     {   if(errno == EISOPEN)
         {  DosSleep(1);
            fd = fopen(buf, "a");
           if(fd) break;
         }
     }
    if(fd == NULL)
    {  sgLogError("%s: can't write to logfile %s, errno=%i",progname,buf,errno);
       return NULL;
    }
  }
  if(stat(buf,&s) != 0){
    sgLogError("%s: can't stat logfile %s",progname,buf);
    return NULL;
  }
  if(LogFileStat == NULL){
    sg = (struct LogFileStat *) sgCalloc(1,sizeof(struct LogFileStat));
    sg->name = (char *) sgMalloc(strlen(buf) + 1);
    strcpy(sg->name,buf);
    sg->st_ino = s.st_ino;
    sg->st_dev = s.st_dev;
    sg->fd = fd;
    sg->next = NULL;
    LogFileStat = sg;
    lastLogFileStat = sg;
  } else {
    for(sg = LogFileStat; sg != NULL; sg = sg->next){
      if(sg->st_ino == s.st_ino && sg->st_dev == s.st_dev){
       fclose(fd);
       return sg;
      }
    }
    sg = (struct LogFileStat *) sgCalloc(1,sizeof(struct LogFileStat));
    sg->name = (char *) sgMalloc(strlen(buf) + 1);
    strcpy(sg->name,buf);
    sg->st_ino = s.st_ino;
    sg->st_dev = s.st_dev;
    sg->fd = fd;
    sg->next = NULL;
    lastLogFileStat->next = sg;
    lastLogFileStat = sg;
  }
  return lastLogFileStat;
}

/* �஢����� � ��८��筮���� checktime, ��������� �� 䠩� fname */
int sgCheckForReloadConfig(char *fname,int checktime)
{
       struct stat s;
static struct stat olds;
static int t0=0;
     int t,rc;
    t = clock();
    if(abs(t-t0) < checktime)
                 return 0;
    t0 = t;
    if(stat(fname,&s) != 0)
    {  sgLogError("%s: can't find file %s",progname,fname);
       return 0;
    }
/*
      struct stat
         {
         dev_t st_dev;
         ino_t st_ino;
         unsigned short st_mode;
         short st_nlink;
         short st_uid;
         short st_gid;
         dev_t st_rdev;
         unsigned short __filler;
         off_t st_size;
         time_t st_atime;
         time_t st_mtime;
         time_t st_ctime;
         };
*/
    rc = 0;
    if(olds.st_size != s.st_size)
           rc=1;
    if(olds.st_mtime != s.st_mtime)
           rc=1;
    olds = s;
    return rc;
}


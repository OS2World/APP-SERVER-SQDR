/* SQDR client эмулятор */
/* mainEmul.cpp */
#define DEBUG 0

#define INCL_DOS
#define  INCL_BASE
#include <os2.h>

#include <stdio.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <time.h>
#include <signal.h>

 #include <types.h>
 #include <unistd.h>
 #include <sys\socket.h>
 #include <netinet\in.h>
 #include <netnb\nb.h>
 #include <sys\un.h>

#include "getopt.h"

#include "sg.h"
#include "SQDRclient.hpp"

#define VERSION "0.07"

/* Internal functions prototypes */
void usage(void);
int sgReadConfig(char *confname);
char * sg_fgets(char *buf, int buflen, int fd);
int sg_puts(char *buf, int fd);

int SendQueryToServer(char *qurl, char *bufout,int bufoutlen);
int QueryProcessType(void);
int LoadTestFile(char *fname);
char *GetNextUrl(char *buf);


struct LogFileStat *globalErrorLog = NULL;
struct LogFile *globalLogFile = NULL;

struct LogFileStat *lastLogFileStat;
struct LogFileStat *LogFileStat;


char **globalArgv ;
char **globalEnvp ;

int globalDebug = 0;
int globalPid = 0;
char *globalLogDir = NULL;
/*
int globalDebugTimeDelta = 0;
int globalUpdate = 0;
char *globalCreateDb = NULL;
int failsafe_mode = 0;
int sig_hup = 0;
int sig_alrm = 0;
int sgtime = 0;
*/
int sockSquid= -1;
static const char *hello_string = "hi there\n";
char *progname;

char *configFile = "SQDR/redir.rules";

class NPipe SQDRpipe;

int    detachedMode=0;

char SQDRmutexName[] = "\\SEM32\\SQDRserver"; /* Semaphore name */
HMTX   SQDR_hmtx     = NULLHANDLE; /* Mutex semaphore handle */
char TestFname[256];
/**********************/
/* кусочек статистики */
int NS_queries=0;
int NS_t0; // время начала сбора статистики
int NS_t;

int main(int argc, char *argv[], char *envp[])
{
  int ch,i,rep;
static   struct SquidInfo squidInfo;
  struct Source *src;
static  char buf[MAX_BUF],lastquery[MAX_BUF]="",bufout[MAX_BUF];
  char *redirect;
  time_t t;
      int fd=-1;
      int rc, t0,t01;

  squidInfo.protocol[0] = 0;

  progname = argv[0];
  if(argc < 3)
  {  printf("Usage: Emul Nclient TestFname");
     exit(1);
  }
  globalPid = atoi(argv[1]);
  strcpy(TestFname, argv[2]);
  LoadTestFile(TestFname);

  sgSetGlobalErrorLogFile();


  DosSleep(10+(globalPid)*750);

/**********************************************/
  sgLogError("sqdrClient %s started", VERSION);
   if( QueryProcessType() == 4)
   {   detachedMode = 1;
       sgLogError("detached mode");
   }

   rc = DosOpenMutexSem(SQDRmutexName,      /* Semaphore name */
                         &SQDR_hmtx);                 /* Handle returned */

//printf("[%i] DosOpenMutexSem rc=%i  ",globalPid,rc); fflush(stdout);

    DosCloseMutexSem(SQDR_hmtx);


/* запускаем сервер - если он уже запущен - сам вывалится */
   if(rc)
   {
      if(detachedMode)
      {  rc = system("detach sqdrserver");
         sgLogError("detach sqdrserver, rc=%i",rc);
         if(rc == -1)
         {  sgLogError("errno =%i, _doserrno=%i",errno,_doserrno);
         }
      } else {
//         printf("[%i]start /N sqdrserver ",globalPid); fflush(stdout);
         rc = system("start /N sqdrserver");
//         printf("[%i]rc=%i\n",globalPid,rc); fflush(stdout);
//         sgLogError("start /N sqdrserver, rc=%i",rc);
        if(rc == -1)
        {
           sgLogError("errno =%i, _doserrno=%i",errno,_doserrno);
        }
     }
//   system("start sqdrserver");
     DosSleep(200+globalPid*50);
   }
   for(rep=0,i=0;i<MAX_NUM_PIPES,rep<40+MAX_NUM_PIPES;i++)
   {  if(i) sprintf(buf,"%s%i",SQDR_BASE_PIPE_NAME,i);
      else strcpy(buf,SQDR_BASE_PIPE_NAME);

      SQDRpipe = NPipe(buf,CLIENT_MODE);

      rc = SQDRpipe.Open();
      if(rc == ERROR_PIPE_BUSY)
               continue;
      if(rc == ERROR_PATH_NOT_FOUND)
      {  rep++;
         DosSleep(100+rep*10);
         i--;
         if(rep == MAX_NUM_PIPES) i = -1;
         continue;
      }
      if(rc)
      {  printf("Error open pipe rc=%i",rc);
         sgLogError("Error open pipe rc=%i, Exitting",rc);
         if(rc == ERROR_INVALID_PARAMETER) printf("(Hедопустимый паpаметp");
         exit(1);
      }
      break;
    }
    if(!SQDRpipe.Hpipe)
    {  printf("Can't open pipe(s)");
       sgLogError("Can't open pipe(s), Exitting");
       exit(2);
    }
//    printf("Open pipe=%i\n",rc);
    rc = SQDRpipe.HandShake();
//    printf("HandShake=%i\n",rc);
    if(rc ==  HAND_SHAKE_ERROR)
    {
        printf("Error handshake\n",rc);
        sgLogError("Error handshake  pipe, Exitting");
        exit(1);
    }

  NS_t0 = clock(); // время начала сбора статистики

  while(1)
  {  char *pbuf;
     t0 = clock();
//    while(fgets(buf, MAX_BUF, stdin) != NULL)
     while( GetNextUrl(buf) != NULL)

    {
       i = strlen(buf);
       if(buf[i-1] == '\n') buf[i-1] = 0;
#if DEBUG
printf(">%s<\n",buf);
#endif
       t01 = clock();
       if(abs(t0-t01) > 5000 || strcmp(buf,lastquery)) /* speed up fast and repiated queryes without any server query */
       {   strcpy(lastquery,buf);
           rc = SendQueryToServer(buf, bufout,sizeof(bufout));
           if(rc)
           {   sgLogError("sqdrэмулятор pipe read error=%i",rc);
               break;
           }
       }
#if DEBUG
printf("<%s, %i ms> \n",bufout,abs(t0-t01));
#endif
       t0 = t01;
      NS_queries++;
      NS_t = clock() - NS_t0;
      if(!(NS_t%1000) && NS_t > 0)
      {  printf("%8.3f\r", (double)NS_queries /(0.001 * NS_t) ); fflush(stdout);
      }
//       sg_puts(bufout, fd);
    }

#if HAVE_SIGNAL
    if(errno != EINTR){
      gettimeofday(&stop_time, NULL);
      stop_time.tv_sec = stop_time.tv_sec + globalDebugTimeDelta;
      sgLogError("sqdrClient эмулятор stopped (%d.%03d)",stop_time.tv_sec,stop_time.tv_usec/1000);
      exit(2);
    }
#endif
//    rc=soclose(fd); //?? or sockSquid
    sgLogError("sqdr client эмулятор stopped, soclose rc=%i",rc);
    exit(0);
  }

  rc=soclose(fd); //?? or/and sockSquid
  sgLogError("sqdr client эмулятор stopped,  soclose  rc=%i",rc);
   _fcloseall();
//      close(sockSquid);
//      close(fd);

  exit(0);
}

void usage(void)
{
  fprintf(stderr,
         "Usage: sqdrClient [-u] [-C block] [-t time] [-c file] [-v] [-d]\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  -v          : show version number\n");
  fprintf(stderr, "  -d          : all errors to stderr\n");
  fprintf(stderr, "  -c file     : load alternate configfile\n");
  fprintf(stderr, "  -t time     : specify staruptime on format: yyyy-mm-ddTHH:MM:SS\n");
  fprintf(stderr, "  -u          : update .db files from .diff files\n");
  fprintf(stderr, "  -C file|all : create new .db files from urls/domain files\n");
  fprintf(stderr, "                specified in \"file\".\n");
  exit(1);
}

/* чтение строки из сокета */
char * sg_fgets(char *buf, int buflen, int fd)
{  int rc;
  if(fd < 0)
       return  fgets(buf, buflen, stdin);
  rc = recv(fd,buf,buflen-1,0);

  if(rc > 0)
  {  buf[rc] = 0;
     return buf;
  }
  else
     return NULL;
}

int sg_puts(char *buf, int fd)
{  int rc;
static  char buff[MAX_BUF];
   if(fd < 0)
   {     puts(buf);
   } else {
      strcpy(buff,buf);
      strcat(buff,"\n");

      rc = send(fd, buff, strlen(buff), 0);
//  sgLogError("send( %x)=%i =>%s<",fd,rc,buff);
      return rc;
  }
  return 0;
}



/************************/
int SendQueryToServer(char *qurl, char *bufout,int bufoutlen)
{  int l,rc,ncmd,data;
   if(!qurl)
      return -1;
   l = strlen(qurl);
   if(!l)
      return -2;
   l++; //+ zero at end of string
   rc = SQDRpipe.SendCmdToServer(1,l); // команда 1 - послать данные о запросе
   if(rc)
      return rc;
   rc = SQDRpipe.SendDataToServer((void *)qurl, l);
   if(rc)
      return rc;
   rc = SQDRpipe.RecvCmdFromClient(&ncmd,&data);
   if(rc)
      return rc;
   switch(ncmd)
   {  case 1:
        l = data;
        rc = SQDRpipe.RecvDataFromClient(bufout,&l,bufoutlen);
          break;

   }

   return rc;
}


int QueryProcessType(void)
{
    PTIB   ptib = NULL;          /* Thread information block structure  */
    PPIB   ppib = NULL;          /* Process information block structure */
    APIRET rc   = NO_ERROR;      /* Return code                         */
    int prtype;

    rc = DosGetInfoBlocks(&ptib, &ppib);
    if (rc != NO_ERROR)
    {  printf ("DosGetInfoBlocks error : rc = %u\n", rc);
          return 1;
    }

    prtype = ppib->pib_ultype;
/*
  pib_ultype (ULONG)
     Process' type code.

     The following process' type codes are available:

     0         Full screen protect-mode session
     1         Requires real mode. Dos emulation.
     2         VIO windowable protect-mode session
     3         Presentation Manager protect-mode session
     4         Detached protect-mode process.

*/
    return prtype;
}

/* загрузка тестового лог-файла */
/* структура данных: char *p[numurls], char *buff[bufflen] */
char *TestBuff;
char * * TestUrls;
int NtestUrls=0;

int LoadTestFile(char *fname)
{   int i,j,l,ll,is, is_section, nline,rc,nurls;
    FILE *fp;
    char str[MAX_BUF], *pstr;
    char strtmp[MAX_BUF], str1[MAX_BUF];

    fp = fopen(fname,"r");
    if(fp == NULL)
    {
       sgLogError("Can't read conf file %s",fname);
       exit(1);
    }
    nline = ll = 0;
    nurls = 0;
/* считаем строки */
     while(fgets(str,MAX_BUF,fp))
     {  nline++;
        l = strlen(str);
        if(l<=3)
            continue;
        is = 0;
        for(i=0;i<3;i++)
        {  if(str[i] > 32)
           {  if(str[i] == '#' || str[i] == ';' ) is = 1;
              break;
           }
        }
        if(is)       /* it is comment */
           continue;
        rc = sscanf(str,"%s %s",strtmp, str1);
        if(rc <= 0)
             continue;
        if(strtmp[0] == 'X') continue;
        nurls++;
        ll += strlen(str1)+1;
     }
     fclose(fp);

    printf("%i \n", nurls); fflush(stdout);

/* alloc memory */
    TestUrls = (char * *) calloc( sizeof(char *), nurls+1);
    TestBuff = (char *) calloc( sizeof(char), ll);
    NtestUrls = nurls;

    fp = fopen(fname,"r");
    if(fp == NULL)
    {  return 1;
    }

    nline = 0;
    nurls = 0;
    j = 0;
    pstr = TestBuff;
     while(fgets(str,MAX_BUF,fp))
     {  nline++;
        l = strlen(str);
        if(l<=3)
            continue;
        is = 0;
        for(i=0;i<3;i++)
        {  if(str[i] > 32)
           {  if(str[i] == '#' || str[i] == ';' ) is = 1;
              break;
           }
        }
        if(is)       /* it is comment */
           continue;
        rc = sscanf(str,"%s %s",strtmp, str1);
        if(rc <= 0)
             continue;
        if(strtmp[0] == 'X') continue;
        nurls++;
        strcpy(pstr,str1);
        TestUrls[j++] = pstr;
        pstr += strlen(str1)+1;


     } /* end of while(fgets) */

     fclose(fp);

    return 0;
}
// http://ad.doubleclick.net/ad 10.0.3.68/- - GET

char *GetNextUrl(char *buf)
{  int n,n1;
   n = rand() % NtestUrls;
   n1 = rand()%250+1;
   sprintf(buf,"%s 195.209.192.%i/- - GET",TestUrls[n],n1);
   return buf;

}

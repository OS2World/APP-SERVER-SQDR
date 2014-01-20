/* SQDR client */
/* main.cpp */
#define DEBUG 0

#define INCL_DOS
#define  INCL_BASE
#include <os2.h>

#include <stdio.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
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

#define VERSION "0.12"

/* Internal functions prototypes */
void usage(void);
int sgReadConfig(char *confname);
char * sg_fgets(char *buf, int buflen, int fd);
int sg_puts(char *buf, int fd);

int SendQueryToServer(char *qurl, char *bufout,int bufoutlen);
int QueryProcessType(void);


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

char *ExternMachine = NULL;   /* Name of external machine for pipes like \\MACHINE\PIPE\SQDR   */
char ExternMachineName[_MAX_FNAME]="";

int  detachedMode=0;

char SQDRmutexName[] = "\\SEM32\\SQDRserver"; /* Semaphore name */
HMTX   SQDR_hmtx     = NULLHANDLE; /* Mutex semaphore handle */

int main(int argc, char *argv[], char *envp[])
{
  int ch,i,ii,rep;
static   struct SquidInfo squidInfo;
  struct Source *src;
static  char buf[MAX_BUF],lastquery[MAX_BUF]="",bufout[MAX_BUF];
  char *redirect;
  FILE *fp;

  time_t t;
      int fd=-1;
      int rc, t0,t01;

  squidInfo.protocol[0] = 0;

  progname = argv[0];

  globalArgv = argv;
  globalEnvp = envp;
/* read client config */
  fp = fopen("SQDR/client.cfg","r");
  if(fp)
  { ExternMachine = fgets(ExternMachineName,128,fp);
  }
  fclose(fp);

  sgSetGlobalErrorLogFile();

  sock_init();

/*********************** разбор командной строки *****************/
   while ((ch = getopt(argc, argv, "hduC:t:c:vs:p:")) != EOF)
    switch (ch) {
    case 'd':
       globalDebug = 1;
      break;
    case 'c':
      configFile = optarg;
      break;
    case 'p':
         globalPid = atoi(optarg) - 6;
         printf("globalPid=%i",globalPid);
      break;
    case 's' :
      sscanf(optarg,"%i -p %i", &sockSquid,&globalPid);
//      printf("S optarg =%s",optarg);
//      printf(" sockSquid=%i, globalPid=%i",sockSquid,globalPid);
      globalPid -= 6;
      sockSquid = atoi(optarg);
            if (sockSquid < 0)
            {  printf("sqdrClient: unable to import socket handle\n");
               sgLogError("unable to import socket handle %x (optarg=%s)",sockSquid,optarg);
              exit(1);
            }
       sgLogError("socket handle %x (optarg=%s)",sockSquid,optarg);
      break;
    case '?':
    case 'h':
    default:
      usage();
    }
  rep = 10+(globalPid)*50;
  if(globalPid) rep += 100;
  DosSleep(rep);

/**********************************************/
printf("sqdrClient %s started", VERSION);
  sgLogError("sqdrClient %s started", VERSION);
   if( QueryProcessType() == 4)
   {   detachedMode = 1;
       sgLogError("detached mode");
   }

    if (sockSquid >= 0)
    {
      fd = accept(sockSquid, NULL, NULL);
printf("fd=%x",fd);
      if (fd < 0)
      {  rc = sock_errno();
         psock_errno(NULL);
         printf("redirector: unable to accept socket handle, rc =%i\n",rc);
        sgLogError("redirector: unable to accept socket handle,rc=%i \n",rc);
        exit(1);
      }
/*********************************************/
      rc = send(fd, hello_string, strlen(hello_string), 0);
/*********************************************/
       if (rc !=  strlen(hello_string) )
      {
            psock_errno(NULL);
           printf ("ipcCreate: CHILD: hello write test failed, rc = %i\n",rc);
           sgLogError("ipcCreate: CHILD: hello write test failed (rc=%i)\n",rc);
       _exit(1);
      }
        else printf("Redirector%i (SQDR)\t\t:OK\n",globalPid);

//     sgLogError("Redirector (SQDR)\t\t:OK fd=%i\n",fd);
    }

/*****************************************************/

    rc = DosOpenMutexSem(SQDRmutexName,      /* Semaphore name */
                         &SQDR_hmtx);                 /* Handle returned */

printf("[%i] DosOpenMutexSem rc=%i  ",globalPid,rc); fflush(stdout);

    DosCloseMutexSem(SQDR_hmtx);


/* запускаем сервер на локальной машине -
   если он таки окажется уже запущен - сам вывалится и повторно запускаться не будет
*/
   if(rc && !ExternMachine)
   {
      if(detachedMode)
      {  rc = system("detach sqdrserver");
         sgLogError("detach sqdrserver, rc=%i",rc);
         if(rc == -1)
         {  sgLogError("errno =%i, _doserrno=%i",errno,_doserrno);
         }
      } else {
//Squid2.3s4 printf("[%i]start /N sqdrserver ",globalPid); fflush(stdout);
//Squid2.3s4         rc = system("start /N sqdrserver");
         printf("[%i]start sqdrserver ",globalPid); fflush(stdout);
         rc = system("start \"SQDR\" /B /C sqdrserver"); //SQUID 2.4s2

//         printf("[%i]rc=%i\n",globalPid,rc); fflush(stdout);
//         sgLogError("start /N sqdrserver, rc=%i",rc);
        if(rc == -1)
        {
           sgLogError("errno =%i, _doserrno=%i",errno,_doserrno);
        }
     }
     DosSleep(200+globalPid*50);
   }

   for(rep=0,i=0;i<MAX_NUM_PIPES*2,rep<40+MAX_NUM_PIPES;i++)
   {  ii = i % MAX_NUM_PIPES;

      if(ExternMachine)
      {  if(ii) sprintf(buf,"\\\\%s\\%s%i",ExternMachine,SQDR_BASE_PIPE_NAME,ii);
         else sprintf(buf,"\\\\%s\\%s",ExternMachine,SQDR_BASE_PIPE_NAME);
      } else {
         if(ii) sprintf(buf,"%s%i",SQDR_BASE_PIPE_NAME,ii);
         else strcpy(buf,SQDR_BASE_PIPE_NAME);
      }

      SQDRpipe = NPipe(buf,CLIENT_MODE);

      rc = SQDRpipe.Open();
      if(rc == ERROR_PIPE_BUSY)
               continue;
      if(rc == ERROR_PATH_NOT_FOUND)
      {  rep++;
         DosSleep(100+rep*10);
         //i--;
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


  while(1)
  {  char *pbuf;
     t0 = clock();
//hihiEK
     while(fgets(buf, MAX_BUF, stdin) != NULL)
//hihiEK     while( sg_fgets(buf, MAX_BUF, fd) != NULL)

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
           {   sgLogError("sqdr pipe read error=%i",rc);
               break;
           }
       }
#if DEBUG
printf("<%s, %i ms> \n",bufout,abs(t0-t01));
#endif
       t0 = t01;
//EK   sg_puts(bufout, fd);
       if(bufout[0])  
          printf("%s\n",bufout);
       else
          puts("");
       fflush(stdout);

    }

#if HAVE_SIGNAL
    if(errno != EINTR){
      gettimeofday(&stop_time, NULL);
      stop_time.tv_sec = stop_time.tv_sec + globalDebugTimeDelta;
      sgLogError("sqdrClient stopped (%d.%03d)",stop_time.tv_sec,stop_time.tv_usec/1000);
      exit(2);
    }
#endif
    rc=soclose(fd); //?? or sockSquid
    sgLogError("sqdr client stopped, soclose rc=%i",rc);
    exit(0);
  }

  rc=soclose(fd); //?? or/and sockSquid
  sgLogError("sqdr client stopped,  soclose  rc=%i",rc);
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



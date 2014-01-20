/* SQDRserver.cpp */

#define  INCL_BASE
#define INCL_DOSSEMAPHORES      /* Semaphore values */
#define INCL_DOSERRORS          /* DOS Error values */
#define INCL_DOSPROCESS
 #include <os2.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <builtin.h>
#include <time.h>

#include "SQDR.hpp"
#include "SQDRclient.hpp"

#include "sg.h"

#define VERSION "0.12"


/*+---------------------------------+*/
/*| External function prototypes.   |*/
/*+---------------------------------+*/
int InitQueue(void);
int ReadQueue(int threadNum);
int AddToQueue(int threadNum);

/*+---------------------------------+*/
/*| Internal function prototypes.   |*/
/*+---------------------------------+*/
int threadsHits_statistics(void);
int SetupSemaphore(void);
void _Optlink SQDR_cleanup(void);
void _Optlink SQDR_ClientWork(void *param);

int sgCheckForReloadConfig(char *fname,int checktime);
char *MainRedir(struct Source *src, struct SquidInfo *squidInfo, char *redirBuff,
                   int globalPid, int checkUser,int &Classification, int &redircode);

char * sg_fgets(char *buf, int buflen, int fd);
int sg_puts(char *buf, int fd);

int _Optlink RedirSortCmp(const void* s1, const void* s2);
int DelDuplicity(struct Eredir2 *red, int &Nred);
int HandleClientQuery(char *buf, char *bufout, int nclient);
int HandleAnalyzerQuery(char *buf, char *bufout, int nclient,int &Classification, int &redircode);

int QueryProcessType(void);
char * FilterUrl(char * purl);

/*+--------------------------------------------------------+*/
/*| Static global variables and local constants.           |*/
/*+--------------------------------------------------------+*/
HMTX    SQDR_hmtx     = NULLHANDLE; /* Mutex semaphore handle */

class NPipe SQDR_pipe[MAX_NUM_THREADS];
struct SQDR_threads
{
   volatile int semaphore;    // семафор для блокировки одновременного доступа к остальным полям
   int num;          // число ниток
   int next_free;    // следующая свободная
   int working;      // =0 на старте, =1 после появления первого живого клиента (защелка)
   int Nclients;     // число живых клиентов
   int thread_id[MAX_NUM_THREADS]; // id'ы ниток
   int hits_total[MAX_NUM_THREADS]; // запросов всего по ниткам
   int hits_min  [MAX_NUM_THREADS]; // запросов за минуту по ниткам
   int hits_hour [MAX_NUM_THREADS]; // запросов за час    по ниткам
   int state [MAX_NUM_THREADS];     // состояние нитки: 0-свободно, 1 - занято
};

struct SQDR_threads  SQDRthreads = { 0,0,0,0,0 };

volatile int semnumQueryInProgress=0; // семафор количества обрабатываемых запросов
volatile int numQueryInProgress=0;    // количество обрабатываемых запросов
volatile int semReconfigInProgress=0; // семафор реконфигурации
/* при выставленном semReconfigInProgress обработка запроса не производится (уходит в sleep),
   numQueryInProgress не увеличивается, чтение конфигурации начинается при numQueryInProgress=0
*/


struct LogFileStat *globalErrorLog = NULL;
struct LogFile *globalLogFile = NULL;
struct LogFileStat *lastLogFileStat;
struct LogFileStat *LogFileStat;
//int sig_hup = 0;
char *globalLogDir = NULL;
char *progname;

char *configFile = "SQDR/redir.rules";
char *cWord_BaseRedirUrl = "BaseRedirUrl";
char *cWord_DefaultRedir = "DefaultRedir";
char *cWord_RedirRules   = "RedirRules";
char *cWord_BlackList   =  "BlackList";
char *cWord_BlackListUpdate     =  "UpdateBlack_List";
char *cWord_BlackListMaxHitsPerHour   =  "MaxHitsPerHourBlack_List";
char *cWord_BlackListWait_min   =  "Wait_minBlack_List";
char *cWord_BlackListWaitUrl    =  "WaitUrlBlack_List";
char *cWord_UserInfoFname    =  "UsersInfo";
char *cWord_maxloglength    =  "maxloglength";


char const  sg_strhttp[]="http://",
            sg_strwww[]="www",
            sg_strwwwdot[]="www.",
            sg_strad[]="ad",
            sg_stradv[]="adv";


//максимальный размер таблицы
#define MAX_REDIR_TABLE       1024
#define MAX_URL_BUFF        3409600
#define MAX_REDIR_URL_BUFF    5120
#define MAX_CLASSIFICATION     64

int  numClassification = 0;             /* число классификаций    */
int  curClassification = 0;             /* текущая  */
char sClassification[MAX_CLASSIFICATION][40]; /* классификация ресурсов */

#define MAX_USERS   256
/*******************************/
struct ClientHitsStatisticPerHour
{  int hits;    // всего обращений
   int redirs;  // из них редирекчено
   int porno;   // в том числе порно
   int banned;   // в том числе banned
};

class ProxyUser
{
public:
   char name[80];
   char src[128];
   struct ClientHitsStatisticPerHour sth[25];

   time_t TlastHit; //время последнего обращения
   int sts; // состояние элемента класса клиента = 0 - не инициализирован, 1 - инициализирован,
            // 3 - отдыхает от порнухи
   int usePersonalPar;          // 1/0 - use personal parameters
   int BlackListMaxHitsPerHour; // personal
   int BlackListWait_min;       // personal

   ProxyUser(void)
   {  InitUser();
      name[0] = src[0] = 0;
      usePersonalPar = 0;
      BlackListMaxHitsPerHour = BlackListWait_min = 10;
   }
   void InitUser(void)
   {  int i;
      sts=0;
      for(i=0;i<24;i++)
      { sth[i].hits = sth[i].redirs = sth[i].porno = sth[i].banned = 0;
      }
   }

   void AddHit(int type, int threadid)
   {   time_t t;
       struct tm  *dtm;
       int hour;
   /* type = pure hit, hit with redirect, hit with pornoredirect */
       time(&t);
       TlastHit = t;
       dtm = localtime(&t);
       hour = dtm->tm_hour;
       sth[hour].hits++;
       if(type == 1)      sth[hour].redirs++;
       else if(type == 2) sth[hour].porno++;
       SQDRthreads.hits_total[threadid]++; // запросов всего по ниткам
       SQDRthreads.hits_min  [threadid]++; // запросов за минуту по ниткам
       SQDRthreads.hits_hour [threadid]++; // запросов за час    по ниткам

   }

   int CheckUserForBanned(int _blackListMaxHitsPerHour,int _blackListWait_min)
   {   time_t t;
       struct tm  *dtm;
       int hour,rc=0;

       time(&t);
       dtm = localtime(&t);
       hour = dtm->tm_hour;
       if(usePersonalPar)
       {  _blackListMaxHitsPerHour = BlackListMaxHitsPerHour;
          _blackListWait_min       = BlackListWait_min;
       }

       if(sth[hour].porno > _blackListMaxHitsPerHour * (sth[hour].banned+1))
       {
          if( difftime(t, TlastHit) <  (_blackListWait_min + sth[hour].banned*2) * 60)
          {
               rc = 2;
               if(sts == 1) rc = 1;
               sts = 3;
          } else {
               sts = 1;
               sth[hour].banned++;
          }

       } else {
             sts = 1;
       }
       return rc;
   }

   int savelog(void);
};
/*******************************/

struct Eredir2
{  char *url;
   char *redirurl;
   int line;   // исходная строка рулесов
   int type;   // тип рулесов: 0 - сравнение с начала, 1 - подстрока, 2 - сравнение с конца
   short int section;//номер секции (баннеры/счетчики/чаты/порно/файлы)
   short int classification; //категория - баннеры/счетчики/чаты/порно/видео/файлы/etc. - для ответов анализатору
};

class ReDirCategory
{
public:
   int  Nredir;     // длина redir2
   int  Nredir_any; // длина redir_any
   int  Nredir_end; // длина redir_end
   int  Nredir_abort; // длина redir_abort

   int nA_redir2;   // выделенная длина redir2 /* не в байтах */
   int nA_redir_end;
   int nA_redir_any;
   int nA_redir_abort;
   struct Eredir2 *redir2;    // таблица урлов с точным началом
   struct Eredir2 *redir_end; // таблица урлов с точным концом (ляля$)
   struct Eredir2 *redir_any; // таблица подстрок урлов
   struct Eredir2 *redir_abort; // таблица подстрок урлов c точным началом, для которых не будет производится проверка

   ReDirCategory()
   {
        Nredir     = 0;
        Nredir_any = 0;
        Nredir_end = 0;
        Nredir_abort = 0;
        redir2    = NULL;
        redir_end = NULL;
        redir_any = NULL;
        redir_abort = NULL;
        nA_redir2 = 0;
        nA_redir_end = 0;
        nA_redir_any = 0;
        nA_redir_abort = 0;
   }

   ~ReDirCategory()
   {  if(redir2)
          free(redir2);
      if(redir_end)
          free(redir_end);
      if(redir_any)
          free(redir_any);
      if(redir_abort)
          free(redir_abort);
   }

   int Add_ReDir_any(char *url, char *redir, int nline, int nfile, int classification);
   int Add_ReDir_end(char *url, char *redir, int nline, int nfile, int classification);
   int Add_ReDir(char *url, char *redir, int nline, int nfile,int classification);
   int Add_ReDir_abort(char *url, char *redir, int nline, int nfile, int classification);
   int Add_ReDir_redirurl(char *redirurl, int mode);
   int ReDir_SortIndex(void);
   int RedirSearch(struct Eredir2 redir[],int _nred,char *url, int *Error);
   int RedirSearch_any(char *url, int *Error);
   int RedirSearch_end(char *url, int *Error);
   int RedirCheck(int iduser, char *purl, char *redirBuff, char *orig, char * * pcrc, int *rid);

};

class ReDirMain
{
public:
     FILE *fplog;     // fp лог-файла
     char BaseRedirUrl[256];
     char DefaultRedir[256];
     char BlackList[256];
     char BlackListUpdate[256];
     char UsersInfoFname[256]; /* filename with user's info */
     char *BlackList2;
     int maxloglength; /* максимальная длина лог-файла при запуске SQDR */
   int  NredirUrls; // длина pRedirUrl
   int  L_UrlBuff;
   int  L_RedirUrl;

   char UrlBuff[MAX_URL_BUFF];
   char RedirUrlBuff[MAX_REDIR_URL_BUFF];
   char *pRedirUrl[MAX_REDIR_TABLE];
   class ReDirCategory rd;
   class ReDirCategory rdblack;

   int BlackListMaxHitsPerHour;
   int BlackListWait_min;
   char *BlackListWaitUrl[16];
   char BlackListWaitUrlBuff[512];
   int nBlackListWaitUrl;
   int iBlackListWaitUrl;

   int nusers;
   int lastLogsSaveHour;      //когда последний раз сохранялись логи и очищалась статистика
   int lastLogsSaveDay;      //когда последний раз сохранялись логи и очищалась статистика

   char *fileNames[256];    //массив указателей на имена используемых файлов
   int NfileNames;          //сколько именфайлов используется

     ReDirMain()
     {  int i;
        fplog=NULL;
        for(i=0;i<256;i++) fileNames[i] = NULL;
        NfileNames = 0;
        Init();
        rdblack.Nredir     = 0;
        rdblack.Nredir_any = 0;
        rdblack.Nredir_end = 0;
        BlackListMaxHitsPerHour = 10;
        BlackListWait_min = 10;
        nusers = 0;
        BlackList2 = NULL;
        BlackListWaitUrl[0] = BlackListWaitUrlBuff;
        strcpy(BlackListWaitUrl[0],"images/dot.gif");
        nBlackListWaitUrl=1;
        iBlackListWaitUrl=0;
        lastLogsSaveHour = -1; //-1 for start
        maxloglength = 1024*1024*20; /* 20Mb default */
     }

     int Init(void)
     {
        strcpy(BaseRedirUrl,"http://127.0.0.1");
        strcpy(DefaultRedir,"images/dot.gif");
        strcpy(BlackList,"SQDR\\domains");
        strcpy(UsersInfoFname,"SQDR\\Users.conf");
        NredirUrls = 0; // длина pRedirUrl
        L_UrlBuff  = 0;
        L_RedirUrl = 0;
        rd.Nredir     = 0;
        rd.Nredir_any = 0;
        rd.Nredir_end = 0;


        return 0;
     }
     int sgReadConfig(char *confname);
     int writeBackConfig(char *configFile, int mode);
     int writeBackConfig2(char *fname, int nsection, int mode);
     int sgLoadBlackList(void);
     int CheckUser(char *usersrc);
     int AddUser(char *usersrc, char *username, int maxhit, int maxwait);
     int ReadUsersInfo(void);

     int CheckForSaveHour(void);
};


class ReDirMain rdmain;
class ProxyUser user[MAX_USERS];


int detachedMode=0;

int main(int argc, char *argv[], char *envp[])
{ int i,rc,id, idd;


  if(argc > 1)
  {  if(!stricmp(argv[1],"-r") ) /* only to read, sort, del doubllings and write back rules */
     {  int mode=0;
        if(argc >= 3 && !stricmp(argv[2],"-c"))
                             mode = 1;
        sgSetGlobalErrorLogFile();
        rdmain.sgReadConfig(configFile);
        rdmain.writeBackConfig(configFile, mode);
        rdmain.writeBackConfig2(rdmain.BlackList, 1, mode);
        rdmain.writeBackConfig2(rdmain.BlackListUpdate, 2, mode);
        exit(2);
     } else if(!stricmp(argv[1],"-m") && argc >= 3) /* merge with new/upgraded (porno) domains list */
     {  int mode=0;
        if(argc >= 4 && !stricmp(argv[3],"-c"))
                             mode = 1;
        sgSetGlobalErrorLogFile();
        rdmain.BlackList2 = argv[2];
        rdmain.sgReadConfig(configFile);
        rdmain.writeBackConfig(configFile, mode);
        rdmain.writeBackConfig2(rdmain.BlackList, 1, mode);
        rdmain.writeBackConfig2(rdmain.BlackListUpdate, 2, mode);
        rdmain.writeBackConfig2(rdmain.BlackList2, 3, mode);
        exit(3);
     } else {
        printf("SQDRserver vers %s\n", VERSION);
        printf("Usage:\n");
        printf("SQDRserver\t\tor\n");
        printf("SQDRserver -r [-c]\tfor rules rewrite\n");
        printf("SQDRserver -m newdomain [-c]\tfor merge with new domains file\n");
        printf("\t\t[-c]\toptional compress\n");

     }
  }
   atexit(SQDR_cleanup);
/* семафор надо устанавливать сразу */
   rc = SetupSemaphore();
   if(rc)
     exit(rc);

   InitQueue();

   progname = argv[0];

   sgSetGlobalErrorLogFile();
   rdmain.sgReadConfig(configFile);
   sgCheckForReloadConfig(configFile,0);

   sgSetGlobalErrorLogFile();

   rc = QueryProcessType();
   if(rc == 4)
       detachedMode = 1;
   if(detachedMode)
   {     sgLogError("SQDR server %s started in detached mode", VERSION);
   } else {
         sgLogError("SQDR server %s started",   VERSION);
             printf("SQDR server %s started\n", VERSION);
   }

   rdmain.fplog= fopen("SQDR/redir.log","a");

   for(i=0;i<MAX_NUM_THREADS;i++)
   { SQDRthreads.thread_id[i] = -1;
      SQDRthreads.hits_total[i] = 0; // запросов всего по ниткам
      SQDRthreads.hits_min  [i] = 0; // запросов за минуту по ниткам
      SQDRthreads.hits_hour [i] = 0; // запросов за час    по ниткам
   }
//printf("STACK_SIZE=%i,MAX_BUF=%i\n",STACK_SIZE,MAX_BUF); fflush(stdout);
//printf("32000UL + MAX_BUF*13=%i\n",32000UL + MAX_BUF*13); fflush(stdout);

   idd = SQDRthreads.next_free;
   id = _beginthread(SQDR_ClientWork,NULL, STACK_SIZE,(void *) &idd);


   while(__lxchg(&SQDRthreads.semaphore, LOCKED)) DosSleep(1);
   SQDRthreads.num++;
   SQDRthreads.thread_id[SQDRthreads.next_free] = id;
   if(SQDRthreads.thread_id[SQDRthreads.next_free+1] == -1) SQDRthreads.next_free++;
   else
   {  for(i=0;i<MAX_NUM_THREADS;i++)
      {  if(SQDRthreads.thread_id[i] == -1) SQDRthreads.next_free = i;
      }
   }
   __lxchg(&SQDRthreads.semaphore, UNLOCKED);

/* wait for first client connect */
   do
   {  DosSleep(1000);
   } while (!SQDRthreads.working);

  /* wait for all threads exit */
   do
   {  DosSleep(1000);
      threadsHits_statistics();
   } while (SQDRthreads.Nclients);
  if(!detachedMode)
        printf("Exitting...\n");
   DosSleep(500);

}

int threadsHits_statistics(void)
{
/* Very non-precision timer */
static int t_sec=0;
static int t_min=0;
static int t_hour=0;
static int last_allhits=0, last_hits_min=0,last_hits_hour=0;
static int max_hits_hour=0, max_hits_min=0;
       int i,allhits, hits_hour,hits_min, activthreads;
       char str[400],str1[80];;

      if(++t_sec == 60)
      {
          if(++t_min == 60)
          {  ++t_hour;
               t_min = 0;
          }
          t_sec = 0;
      }

      if(t_min == 0 && t_hour == 0)
                               return 0;
      activthreads = 0;
      for(allhits=hits_hour=hits_min=i=0;i<SQDRthreads.num;i++)
      {       allhits   += SQDRthreads.hits_total[i];
              hits_hour += SQDRthreads.hits_hour[i];
              hits_min  += SQDRthreads.hits_min[i];
              activthreads += SQDRthreads.state[i];
      }

      if(last_allhits == allhits)
      {
        printf("%i, %i, %i %2i:%2i:%2i \r",(last_hits_min+hits_min)/2,last_hits_hour,allhits,
                                  t_hour,t_min,t_sec);
                fflush(stdout);
      }
      last_allhits = allhits;

      if(hits_hour >  max_hits_hour)
                 max_hits_hour = hits_hour;
      if(hits_min >  max_hits_min)
                 max_hits_min = hits_min;

     if(t_sec == 0) /* min begining */
     {
         for(i=0;i < SQDRthreads.num; i++) SQDRthreads.hits_min[i] = 0;
         last_hits_min = hits_min;

       if(t_min == 0) /* hour begining */
       {  sprintf(str,"Hits:%i, hits/hour:%i (",allhits,hits_hour);
          for(i=0;i < SQDRthreads.num; i++)
          {  sprintf(str1,"%i",SQDRthreads.hits_hour[i]);
             strcat(str,str1);
             if(i < SQDRthreads.num-1)
                           strcat(str,", ");
             SQDRthreads.hits_hour[i] = 0;
          }
          last_hits_hour= hits_hour;
          strcat(str,")");
          sprintf(str1,",Max/h=%i, Max/min=%i",max_hits_hour,max_hits_min);
          strcat(str,str1);
          sprintf(str1,"act.threads %i",activthreads);
          strcat(str,str1);
          sgLogError("%s",str);
       }
     } /* endof if(t_sec == 0) */

      return 0;
}


/************************************/
void _Optlink SQDR_ClientWork(void *param)
{   int i,rc, threadNum,id,idd;
    int ncmd,data,l;
    char str[512];
    char buf[MAX_BUF], bufout[MAX_BUF];

//   HAB   habThread4 = NULLHANDLE;           /* anchor block handle for thread */
//   HMQ   hmqThread4 = NULLHANDLE;           /* message queue handle           */
//   QMSG  qmsgThread4;                       /* message queue structure        */

//   habThread4 = WinInitialize( 0UL );
//   hmqThread4 = WinCreateMsgQueue( habThread4, 0UL );

    _control87(EM_UNDERFLOW,EM_UNDERFLOW);

    threadNum =  SQDRthreads.next_free;
    if(param)
           threadNum = * ((int *)param);
  if(!detachedMode)
    printf("Start thread %i \n",threadNum);
    sgLogError("Start thread %i\n",threadNum);
    DosSleep(300);
/* */
     if(threadNum) sprintf(str,"%s%i",SQDR_BASE_PIPE_NAME,threadNum);
     else strcpy(str,SQDR_BASE_PIPE_NAME);
    SQDR_pipe[threadNum] = NPipe(str,SERVER_MODE);
    rc = SQDR_pipe[threadNum].Create();
    if(rc)
    {  printf("Error pipe creating  %s rc=%i",str,rc);
       if(rc == ERROR_INVALID_PARAMETER)
                   printf("(INVALID PARAMETER)");
       sgLogError("Error pipe creating %s rc=%i",str,rc);

       exit(1);

    }
    rc = SQDR_pipe[threadNum].Connect();
    if(rc)
    {   sgLogError("Error connectint pipe rc=%i, exit",rc);
        exit(1);
    }
    rc = SQDR_pipe[threadNum].HandShake();
    if(rc)
    {   sgLogError("Error HandShake pipe rc=%i, exit",rc);
        exit(1);
    }

/***********/
   idd = SQDRthreads.next_free;
   id = _beginthread(SQDR_ClientWork,NULL, STACK_SIZE,&idd);
   while(__lxchg(&SQDRthreads.semaphore, LOCKED)) DosSleep(1);
    SQDRthreads.Nclients++;     // число живых клиентов
    SQDRthreads.working = 1;    // =0 на старте, =1 после появления первого живого клиента (защелка)
   SQDRthreads.num++;
   SQDRthreads.thread_id[SQDRthreads.next_free] = id;
   if(SQDRthreads.thread_id[SQDRthreads.next_free+1] == -1) SQDRthreads.next_free++;
   else
   {  for(i=0;i<MAX_NUM_THREADS;i++)
      {  if(SQDRthreads.thread_id[i] == -1) SQDRthreads.next_free = i;
      }
   }
   __lxchg(&SQDRthreads.semaphore, UNLOCKED);
    SQDRthreads.state[threadNum]=0;
    DosSetPriority( PRTYS_THREAD, PRTYC_REGULAR, +15L, 0UL );

/* команды              */
/* 0 - закончить        */
/* 1 - запрос клиента   */
/* 2 - прервать вав     */
   do
   {
      SQDRthreads.state[threadNum]=0;
      rc = SQDR_pipe[threadNum].RecvCmdFromClient(&ncmd,&data);
      if(rc)
      {
         if(rc == -1)
         {  rc = SQDR_pipe[threadNum].QueryState();
            if(rc == ERROR_BAD_PIPE || rc == ERROR_PIPE_NOT_CONNECTED)
                                  break; // клиент подох ??
         }
         if(!detachedMode)
                printf("Recv error=%i\n",rc);
          sgLogError("Recv error=%i,exitting",rc);
          exit(1);
      }
      SQDRthreads.state[threadNum]=1;
//      printf("Cmd=%i,data=%i\n",ncmd,data);
//   { static int threadNumOld=-1;
//
//      printf("Cmd=%i threadNum,=%i\r",ncmd, threadNum);
//     if(threadNum != threadNumOld)  printf("\n");
//     threadNumOld = threadNum;
//
//   }

      switch(ncmd)
      {
         case 1: /* обычный запрос редиректора */
               rc = SQDR_pipe[threadNum].RecvDataFromClient(buf,&l, sizeof(buf));
//               printf("%s",buf);
               HandleClientQuery(buf,bufout,threadNum);
               l = strlen(bufout) + 1;
               rc=SQDR_pipe[threadNum].SendCmdToServer(1, l);
               rc=SQDR_pipe[threadNum].SendDataToServer(bufout, l);

            break;
         case 2: /* запрос анализатора - классификация урла */
             { int Classification, redircode, idata[2];
               rc = SQDR_pipe[threadNum].RecvDataFromClient(buf,&l, sizeof(buf));
               HandleAnalyzerQuery(buf,bufout,threadNum,Classification, redircode);

               idata[0] = redircode;
               idata[1] = Classification;
               rc=SQDR_pipe[threadNum].SendCmdToServer(2, l);
               rc=SQDR_pipe[threadNum].SendDataToServer(idata, sizeof(int) * 2);
             }
            break;
         case 3: /* запрос анализатора - наименование id'а классификации */
            {   char str[40];
                str[0] = 0;
                if(data>=0 && data < numClassification)
                {   strncpy(str,sClassification[data],40);
                }
               rc=SQDR_pipe[threadNum].SendCmdToServer(3, l);
               rc=SQDR_pipe[threadNum].SendDataToServer(str, sizeof(char) * 40);
            }
            break;

      }

   } while(ncmd);

    SQDRthreads.state[threadNum]=0;
    SQDR_pipe[threadNum].Close();

   DosSetPriority( PRTYS_THREAD, PRTYC_REGULAR, -15L, 0UL );

   while(__lxchg(&SQDRthreads.semaphore, LOCKED))
                                       DosSleep(1);
   SQDRthreads.num--;
   SQDRthreads.thread_id[threadNum] = -1;
   SQDRthreads.next_free = threadNum;
   SQDRthreads.Nclients--;     // число живых клиентов
   __lxchg(&SQDRthreads.semaphore, UNLOCKED);

    DosSleep(500);

//   if ( hmqThread4 != NULLHANDLE )
//      WinDestroyMsgQueue( hmqThread4 );
//   if ( habThread4 != NULLHANDLE )
//      WinTerminate( habThread4 );

//  _endthread();
}




/**************************************/
void SQDR_cleanup(void)
{   int rc,i,j;
    int nhits, nredirs, nporno,nbanned;
    nhits = nredirs = nporno = nbanned = 0;
   for(i= 0; i < rdmain.nusers; i++)
   {  if(!user[i].sts) continue;
      for(j=0;j<24;j++)
      {   nhits += user[i].sth[j].hits;
          nredirs += user[i].sth[j].redirs;
          nporno += user[i].sth[j].porno;
          nbanned += user[i].sth[j].banned;
      }
      user[i].savelog();
   }
   if(rdmain.fplog)
   {  sgLogError("SQDR stop, %i hits, %i redirs, %i pornoredirs, %i banned",
                               nhits,nredirs,nporno,nbanned);
      fclose(rdmain.fplog);
   }


/* в последнюю очередь освобождаем семафор */
    if(SQDR_hmtx)
    {   rc = DosReleaseMutexSem(SQDR_hmtx);        /* Relinquish ownership */
        rc = DosCloseMutexSem(SQDR_hmtx);          /* Close mutex semaphore */
    }
}

int ProxyUser::savelog(void)
{   int i, jj, is, d;
     FILE *fp;
     char fname[256];

     for(i=0, is = 0;i<24;i++)
     {   if(sth[i].hits) { is=1; break; }
     }
     if(!is)
         return 0;

     strcpy(fname,"SQDR\\CLIENTS\\");

     if(name[0]) strcat(fname,name);
     else        strcat(fname,src);
     strcat(fname,".log");

     fp=fopen(fname, "a");

     fprintf(fp,"Day=%i, Hour=%i\n",rdmain.lastLogsSaveDay ,rdmain.lastLogsSaveHour);

     for(i=0;i<24;i++)
     {  jj = (i + rdmain.lastLogsSaveHour)%24;
        if(!sth[jj].hits) continue;
        d = rdmain.lastLogsSaveDay;
        if(jj < rdmain.lastLogsSaveHour) d++;

        fprintf(fp,"%i %i %i %i %i %i\n", d, jj,
               sth[jj].hits,sth[jj].redirs,sth[jj].porno, sth[jj].banned);
     }

     fclose(fp);

     return 0;
}

int SetupSemaphore(void)
{
 PID     pidOwner = 0;          /* PID of current mutex semaphore owner */
 TID     tidOwner = 0;          /* TID of current mutex semaphore owner */
 ULONG   ulCount  = 0;          /* Request count for the semaphore */
 APIRET  rc       = NO_ERROR;   /* Return code */

    rc = DosCreateMutexSem(SQDRmutexName,      /* Semaphore name */
                           &SQDR_hmtx, 0, FALSE);       /* Handle returned */
    if (rc != NO_ERROR)
    {
       if(rc == ERROR_DUPLICATE_NAME)
              printf("SQDR already running\n");
       else
              printf("DosCreateMutexSem error: return code = %u\n", rc);
       return 1;
     }
  if(!detachedMode)
       printf("DosCreateMutexSem %i\n",__LINE__);
         /* This would normally be done by another unit of work */
    rc = DosOpenMutexSem(SQDRmutexName,      /* Semaphore name */
                         &SQDR_hmtx);                 /* Handle returned */
    if (rc != NO_ERROR) {
       printf("DosOpenMutexSem error: return code = %u\n", rc);
       return 1;
     }
  if(!detachedMode)
       printf("DosOpenMutexSem %i\n",__LINE__);

    rc = DosRequestMutexSem(SQDR_hmtx,      /* Handle of semaphore */
                            (ULONG) 1000);  /* Timeout  */
    if (rc != NO_ERROR) {
       printf("DosRequestMutexSem error: return code = %u\n", rc);
       return 1;
    }
  if(!detachedMode)
      printf("DosRequestMutexSem %i\n",__LINE__);

    rc = DosQueryMutexSem(SQDR_hmtx,         /* Handle of semaphore */
                          &pidOwner,    /* Process ID of owner */
                          &tidOwner,    /* Thread ID of owner */
                          &ulCount);    /* Count */
    if (rc != NO_ERROR) {
       printf("DosQueryMutexSem error: return code = %u\n", rc);
       return 1;
    } else if (!detachedMode)  {
       printf("Semaphore owned by PID %u, TID %u.", pidOwner, tidOwner);
       printf("  Request count is %u.\n", ulCount);
    } /* endif */

  if(!detachedMode)

       printf("DosQueryMutexSem %i\n",__LINE__);

 return NO_ERROR;
 }

/*******************************************/
/*********************************/
/* пишем взад файл с рулесами    */
/*********************************/
int  ReDirMain::writeBackConfig(char *configFile, int mode)
{   FILE *fp, *fpto;
    int i,j,l,is, is_section, nline,rc, is_old;
    char TmpFname[]="tmp.tmp";
    char str[MAX_BUF], *pstr;
    char strtmp[MAX_BUF], str1[MAX_BUF];

    struct Eredir2 *predir;
    int nrd, trd;

    fp = fopen(configFile,"r");
    if(fp == NULL)
    {  sgLogError("Can't read conf file %s",configFile);
       exit(1);
    }
    fpto = fopen(TmpFname,"w");
    if(fpto == NULL)
    {  sgLogError("Can't read conf file %s",TmpFname);
       exit(1);
    }
// base rules
    is_section = 0;
    nline = 0;
    is_old = 0;
    while(fgets(str,MAX_BUF,fp))
    {  nline++;
       l = strlen(str);
       is = 0;
       for(i=0;i<l;i++)
       {  if(str[i] > 32)
          {  if(str[i] == '#' || str[i] == ';' ) is = 1;
             break;
          }
       }
       fputs(str,fpto);
       if(is)       /* it is comment */
              continue;

       pstr = strstr(str,cWord_RedirRules);
       if(pstr)
       {   is_section += 2;
           break;
       }
    }

    is_section = 0;
    while(fgets(str,MAX_BUF,fp))
    {  nline++;
       l = strlen(str);
       if(l<=1)
       {   fputs(str,fpto);
           continue;
       }
       is = 0;
       for(i=0;i<l;i++)
       {  if(str[i] > 32)
          {  if(str[i] == '#' || str[i] == ';' ) is = 1;
             break;
          }
       }
       if(is)       /* it is comment */
       {   fputs(str,fpto);
           continue;
       }
       if(str[0] == '[' && strstr(str,"]"))   /* it is section */
       {   fputs(str,fpto);
           continue;
       }
       str1[0]=0;
       strtmp[0] = 0;
       rc = sscanf(str,"%s %s",strtmp, str1);
       if(rc <= 0)
             continue;
       if(strtmp[0])
       {  char c_strtmplast;
          c_strtmplast = strtmp[strlen(strtmp)-1];
          if(strtmp[0] == '*')  // поиск по подстрокам
          {   trd = 1;
              nrd = rd.Nredir_any;
              predir = rd.redir_any;
          } else if(strtmp[0] == '!') {        // аборт   по подстрокам c начала урла
              trd = 3;
              nrd = rd.Nredir_abort;
              predir = rd.redir_abort;

          } else if(c_strtmplast == '$') {     // поиск с конца урла
              trd = 2;
              nrd = rd.Nredir_end;
              predir = rd.redir_end;
          } else {              // поиск по началу урла
              trd = 0;
              nrd = rd.Nredir;
              predir = rd.redir2;
          }

          is = -1;
          for(j=is_old; j<is_old+10;j++)
          {   if(predir[j].line == nline)
                          {is=j; break;}
          }
          if(is == -1)
              for(j=0; j <nrd   ; j++)
              {   if(predir[j].line == nline) {is=j; break;}
              }
          if(is >= 0)
          {  is_old = is;
             if(trd == 1)
                  fprintf(fpto,"*");
             else if(trd == 3)
                  fprintf(fpto,"!");

             if(trd == 2)
             {    strcpy(str1,predir[is].url);
                  fprintf(fpto,"%s$",strrev(str1) );
             } else
                  fprintf(fpto,"%s",predir[is].url);

            if(predir[is].redirurl && strcmp(predir[is].redirurl, DefaultRedir) )
                    fprintf(fpto," %s", predir[is].redirurl);

          }
          if(is >= 0 || mode == 0) fprintf(fpto,"\n");
       } else {
          fprintf(fpto,"\n");

       }
    }
    fclose(fp);
    fclose(fpto);

    remove(configFile);
    rename(TmpFname,configFile);

    return 0;
}

int  ReDirMain::writeBackConfig2(char *fname, int nsection, int mode)
{   FILE *fp, *fpto;
    int i,j,l,is, is_section, nline,rc, is_old;
    char TmpFname[]="tmp1.tmp";
    char TmpFname1[]="tmp2.tmp";
    char str[MAX_BUF], *pstr;
    char strtmp[MAX_BUF], str1[MAX_BUF];

    struct Eredir2 *predir;
    int nrd, trd, err;

    fp = fopen(fname,"r, blksize=32000");
    if(fp == NULL)
    {  sgLogError("Can't read conf file %s",fname);
       exit(1);
    }
    printf("Write file %s, section %i\n", fname, nsection);
    pstr = TmpFname;
    if(nsection > 1)
        pstr = TmpFname1;

    fpto = fopen(pstr,"w, blksize=32000");
    if(fpto == NULL)
    {  sgLogError("Can't read conf file %s",pstr);
       exit(1);
    }
    nline = 0;
    is_old = 0;
    while(fgets(str,MAX_BUF,fp))
    {  nline++;
       l = strlen(str);
       if(l<=1)
       {   fputs(str,fpto);
           continue;
       }
       is = 0;
       for(i=0;i<l;i++)
       {  if(str[i] > 32)
          {  if(str[i] == '#' || str[i] == ';' ) is = 1;
             break;
          }
       }
       if(is)       /* it is comment */
       {   fputs(str,fpto);
           continue;
       }
       if(str[0] == '[' && strstr(str,"]"))   /* it is section */
       {   fputs(str,fpto);
           continue;
       }
       if(!(nline%100)) printf("%i\r",nline); fflush(stdout);
       str1[0]=0;
       strtmp[0] = 0;
       rc = sscanf(str,"%s %s",strtmp, str1);
       if(rc <= 0)
             continue;
       if(strtmp[0])
       {  char c_strtmplast;
          c_strtmplast = strtmp[strlen(strtmp)-1];
          if(strtmp[0] == '*')  // поиск по подстрокам
          {   trd = 1;
              nrd = rdblack.Nredir_any;
              predir = rdblack.redir_any;
          } else if(strtmp[0] == '!') {        // аборт   по подстрокам c начала урла
              trd = 3;
              nrd = rdblack.Nredir_abort;
              predir = rdblack.redir_abort;

          } else if(c_strtmplast == '$') {     // поиск с конца урла
              trd = 2;
              nrd = rdblack.Nredir_end;
              predir = rdblack.redir_end;
          } else {              // поиск по началу урла
              trd = 0;
              nrd = rdblack.Nredir;
              predir = rdblack.redir2;
/******************/
              is = -1;
              rc = rdblack.RedirSearch(predir, rdblack.Nredir, strtmp, &err);
              if(err == 0)
              {    if(predir[rc].line == nline && predir[rc].section == nsection)
                          {is=rc; }
              }
              if(is >= 0)
              {   fprintf(fpto,"%s",predir[is].url);
                  if(predir[is].redirurl && strcmp(predir[is].redirurl, DefaultRedir) )
                    fprintf(fpto," %s", predir[is].redirurl);
                   fprintf(fpto,"\n");
              } else {
               if(mode == 0)
                   fprintf(fpto,"\n");
              }
              continue;
/******************/
          }

          is = -1;
          for(j=is_old; j<is_old+10;j++)
          {   if(predir[j].line == nline && predir[j].section == nsection)
                          {is=j; break;}
          }
          if(is == -1)
              for(j=0; j <nrd   ; j++)
              {   if(predir[j].line == nline && predir[j].section == nsection)
                          {is=j; break;}
              }
          if(is >= 0)
          {  is_old = is;
             if(trd == 1)
                  fprintf(fpto,"*");
             else if(trd == 3)
                  fprintf(fpto,"!");

             if(trd == 2)
             {    strcpy(str1,predir[is].url);
                  fprintf(fpto,"%s$",strrev(str1) );
             } else
                  fprintf(fpto,"%s",predir[is].url);

            if(predir[is].redirurl && strcmp(predir[is].redirurl, DefaultRedir) )
                    fprintf(fpto," %s", predir[is].redirurl);

          }
          if(is >= 0 || mode == 0)
                   fprintf(fpto,"\n");
       } else {
          fprintf(fpto,"\n");

       }
    }
    fclose(fp);
    fclose(fpto);

    remove(fname);
    pstr = TmpFname;
    if(nsection > 1)
        pstr = TmpFname1;
    rename(pstr,fname);

    return 0;
}

/**************************/
/* читаем файл с рулесами */
/**************************/
int  ReDirMain::sgReadConfig(char *confname)
{   int i,j,l,is, is_section, nline,rc;
    FILE *fp;
    char str[MAX_BUF], *pstr;
    char strtmp[MAX_BUF], str1[MAX_BUF];


    if(NfileNames)
    {  for(i=0;i<NfileNames;i++) if(fileNames[i]) free(fileNames[i]);
       NfileNames = 0;
    }
    fp = fopen(confname,"r");
    if(fp == NULL)
    {
       sgLogError("Can't read conf file %s",confname);
       exit(1);
    }
    Init();
    printf("read %s\r",confname); fflush(stdout);
// base rules
    is_section = 0;
    nline = 0;
    while(fgets(str,MAX_BUF,fp))
    {  nline++;
       l = strlen(str);
       is = 0;
       for(i=0;i<l;i++)
       {  if(str[i] > 32)
          {  if(str[i] == '#' || str[i] == ';' ) is = 1;
             break;
          }
       }
       if(is)       /* it is comment */
          continue;

       pstr = strstr(str,cWord_BaseRedirUrl);
       if(pstr)
       {   pstr += strlen(cWord_BaseRedirUrl);
           pstr = strstr(pstr,"=");
           pstr++;
           sscanf( pstr,"%s",BaseRedirUrl);
           is_section++;
       }
       pstr = strstr(str,cWord_DefaultRedir);
       if(pstr)
       {   pstr += strlen(cWord_DefaultRedir);
           pstr = strstr(pstr,"=");
           pstr++;
           sscanf( pstr,"%s",DefaultRedir);
           is_section++;
       }

       pstr = strstr(str,cWord_BlackList);
       if(pstr)
       {   pstr += strlen(cWord_BlackList);
           pstr = strstr(pstr,"=");
           pstr++;
           sscanf( pstr,"%s",BlackList);
           is_section++;
       }
       pstr = strstr(str,cWord_BlackListUpdate);
       if(pstr)
       {   pstr += strlen(cWord_BlackListUpdate);
           pstr = strstr(pstr,"=");
           pstr++;
           sscanf( pstr,"%s",BlackListUpdate);
           is_section++;
       }
       pstr = strstr(str,cWord_BlackListMaxHitsPerHour);
       if(pstr)
       {   pstr += strlen(cWord_BlackListMaxHitsPerHour);
           pstr = strstr(pstr,"=");
           pstr++;
           sscanf( pstr,"%i",&BlackListMaxHitsPerHour);
           is_section++;
       }

       pstr = strstr(str,cWord_BlackListWait_min);
       if(pstr)
       {   pstr += strlen(cWord_BlackListWait_min);
           pstr = strstr(pstr,"=");
           pstr++;
           sscanf( pstr,"%i",&BlackListWait_min);
           is_section++;
       }

       pstr = strstr(str,cWord_BlackListWaitUrl);
       if(pstr)
       {   pstr += strlen(cWord_BlackListWaitUrl);
           pstr = strstr(pstr,"=");
           pstr++;
           while(*pstr  && *pstr <= 32) pstr++;

           { int ll,ii;
               ll = strlen(pstr);
               strcpy(BlackListWaitUrlBuff,pstr);
               pstr = &BlackListWaitUrlBuff[0];
               nBlackListWaitUrl = 0;
               for(ii=0;ii<ll;ii++)
               { if(ii == 0 || (pstr[ii] > 0 && pstr[ii-1] == 0) )
                 {  BlackListWaitUrl[nBlackListWaitUrl++] = &pstr[ii];
                 }
                 if(pstr[ii] <= 32) pstr[ii] = 0;
               }
               for(ii=0;ii<nBlackListWaitUrl;ii++)
               {  printf(">%s<\n",BlackListWaitUrl[ii] );
               }
           }

//           sscanf( pstr,"%s",BlackListWaitUrl[0],);
           is_section++;
       }

       pstr = strstr(str,cWord_UserInfoFname);
       if(pstr)
       {   pstr += strlen(cWord_UserInfoFname);
           pstr = strstr(pstr,"=");
           pstr++;
           sscanf( pstr,"%s",UsersInfoFname);
           is_section++;
       }

       pstr = strstr(str,cWord_maxloglength);
       if(pstr)
       {   pstr += strlen(cWord_maxloglength);
           pstr = strstr(pstr,"=");
           pstr++;
           sscanf( pstr,"%i",&maxloglength);
           maxloglength *= 1024;
           is_section++;
       }


       pstr = strstr(str,cWord_RedirRules);
       if(pstr)
       {   is_section += 2;
           break;
       }

    }

    if(!is_section)
    {  sgLogError("Not found section %s in conf file %s",cWord_BaseRedirUrl,confname);
       exit(1);
    }
    if(is_section<2)
    {  sgLogError("Not found section %s in conf file %s",cWord_RedirRules,confname);
       exit(1);
    }

//redir rules

    is_section = 0;
    while(fgets(str,MAX_BUF,fp))
    {  nline++;
       l = strlen(str);
       if(l<=1)
           continue;
       is = 0;
       for(i=0;i<l;i++)
       {  if(str[i] > 32)
          {  if(str[i] == '#' || str[i] == ';' ) is = 1;
             break;
          }
       }
       if(is)       /* it is comment */
          continue;
       if(str[0] == '[' && strstr(str,"]"))   /* it is section */
       {    pstr = strstr(str,"]");
            *pstr = 0;
            pstr = &str[1];
            is = 0;
            for(i=0;i<numClassification;i++)
            { if(!strncmp(pstr,sClassification[i],40) )
              {  is = 1; break;
              }
            }
            if(is)
            { curClassification = i;
            } else {
               strncpy(sClassification[numClassification],pstr,40);
               curClassification = numClassification;
               numClassification++;
            }
            continue;
       }

       str1[0]=0;
       strtmp[0] = 0;
       rc = sscanf(str,"%s %s",strtmp, str1);
       if(rc <= 0)
             continue;
       if(strtmp[0])
       {  char c_strtmplast;
          c_strtmplast = strtmp[strlen(strtmp)-1];
          if(strtmp[0] == '*')  // поиск по подстрокам
          {
             if(str1[0])  rd.Add_ReDir_any(strtmp, str1, nline,NfileNames, curClassification);
              else        rd.Add_ReDir_any(strtmp, DefaultRedir,nline,NfileNames, curClassification);
          } else if(strtmp[0] == '!') {        // аборт   по подстрокам c начала урла
              if(str1[0])  rd.Add_ReDir_abort(strtmp, str1, nline,NfileNames, curClassification);
               else        rd.Add_ReDir_abort(strtmp, DefaultRedir,nline,NfileNames, curClassification);

          } else if(c_strtmplast == '$') {     // поиск с конца урла
             if(str1[0])  rd.Add_ReDir_end(strtmp, str1, nline,NfileNames, curClassification);
              else        rd.Add_ReDir_end(strtmp, DefaultRedir,nline,NfileNames, curClassification);

          } else {              // поиск по началу урла
             if(str1[0])  rd.Add_ReDir(strtmp, str1, nline,NfileNames, curClassification);
              else        rd.Add_ReDir(strtmp, DefaultRedir,nline,NfileNames, curClassification);
          }
       }

    }


    fclose(fp);

    fileNames[NfileNames] = (char *) malloc(strlen(confname)+2);
    strcpy(fileNames[NfileNames],confname);
    NfileNames++;

    rd.ReDir_SortIndex();

    if(BlackList[0])
    {  sgLoadBlackList();
       rdblack.ReDir_SortIndex();
    }

    sgLogError("ReDir_SortIndex L_UrlBuff=%i,NredirUrls=%i,L_RedirUrl=%i",
                L_UrlBuff,NredirUrls,L_RedirUrl);
    sgLogError("rd Nredir=%i,Nredir_abort=%i,Nredir_any=%i, Nredir_end=%i",
                rd.Nredir,rd.Nredir_abort,rd.Nredir_any, rd.Nredir_end);
    sgLogError("rbblack Nredir=%i, Nredir_abort=%i, Nredir_any=%i, Nredir_end=%i ",
                 rdblack.Nredir,rdblack.Nredir_abort,rdblack.Nredir_any, rdblack.Nredir_end);
    return 0;
}


int ReDirMain::sgLoadBlackList(void)
{   int i,j,l,is, is_section, nline,nurls, rc,raz;
    FILE *fp;
    char str[MAX_BUF], *pstr;
    char strtmp[MAX_BUF], str1[MAX_BUF];


    fp = fopen( BlackList,"r");
    if(fp == NULL)
    {  if(!detachedMode) printf("Can't open file %s\n",BlackList);
       sgLogError("Can't open file %s",BlackList);
       return 1;
    }
    is_section = 0;
   if(!detachedMode)
        printf("Load %s\n",BlackList); fflush(stdout);
    nline = 0;
    nurls = 0;
    l = 0;
/* считаем строчки */
   for(raz=0;raz<3;raz++)
   {
     while(fgets(str,MAX_BUF,fp))
     {  nline++;
        l = strlen(str);
        is = 0;
        for(i=0;i<l;i++)
        {  if(str[i] > 32)
           {  if(str[i] == '#' || str[i] == ';' ) is = 1;
              break;
           }
        }
        if(is)       /* it is comment */
           continue;
       if(str[0] == '[' && strstr(str,"]"))   /* it is section */
       {   continue;
       }

        nurls++;
        l += strlen(str)+1;
      }
      fclose(fp);
      if(raz == 0)
      {  fp = fopen(BlackListUpdate,"r");
         if(fp == NULL)
            break;
      } else if(raz == 1 && BlackList2) {

         fp = fopen(BlackList2,"r");
         if(fp == NULL)
         {
            printf("Can't open file %s\n",BlackList2);
            sgLogError("Can't open file %s",BlackList2);
              break;
         }
      }
    }
    if(!detachedMode)
        printf("%i \n", nurls); fflush(stdout);

/* alloc memory */
//    rd.pRedirBlackList = (struct Eredir2 *) calloc( sizeof(struct Eredir2), nurls+1);
//    rd.pBlackListBuf = (char *) calloc(1,l+1);
      rdblack.Nredir     = 0;
      rdblack.Nredir_any = 0;
      rdblack.Nredir_end = 0;


    fp = fopen(BlackList,"r");
    if(fp == NULL)
    {  return 1;
    }

    fileNames[NfileNames] = (char *) malloc(strlen(BlackList)+2);
    strcpy(fileNames[NfileNames],BlackList);

    nline = 0;
    is_section = 0;
   for(raz=0;raz<3;raz++)
   {
     while(fgets(str,MAX_BUF,fp))
     {  nline++;
        l = strlen(str);
        if(l<=1)
            continue;
        is = 0;
        for(i=0;i<l;i++)
        {  if(str[i] > 32)
           {  if(str[i] == '#' || str[i] == ';' ) is = 1;
              break;
           }
        }
        if(is)       /* it is comment */
           continue;
        if(str[0] == '[' && strstr(str,"]"))   /* it is section */
        {    pstr = strstr(str,"]");
            *pstr = 0;
            pstr = &str[1];
            is = 0;
            for(i=0;i<numClassification;i++)
            { if(!strncmp(pstr,sClassification[i],40) )
              {  is = 1; break;
              }
            }
            if(is)
            { curClassification = i;
            } else {
               strncpy(sClassification[numClassification],pstr,40);
               curClassification = numClassification;
               numClassification++;
            }
            continue;
        }

        str1[0]=0;
        strtmp[0] = 0;
        rc = sscanf(str,"%s %s",strtmp, str1);
        if(rc <= 0)
             continue;
        if(strtmp[0])
        {  char c_strtmplast;
           c_strtmplast = strtmp[strlen(strtmp)-1];
           if(strtmp[0] == '*')  // поиск по подстрокам c начала урла
           {
              if(str1[0])  rdblack.Add_ReDir_any(strtmp, str1, nline,NfileNames,curClassification);
               else        rdblack.Add_ReDir_any(strtmp, DefaultRedir,nline,NfileNames,curClassification);
           } else if(strtmp[0] == '!') {        // аборт   по подстрокам c начала урла
              if(str1[0])  rdblack.Add_ReDir_abort(strtmp, str1, nline,NfileNames,curClassification);
               else        rdblack.Add_ReDir_abort(strtmp, DefaultRedir,nline,NfileNames,curClassification);
           } else if(c_strtmplast == '$') {     // поиск с конца урла
              if(str1[0])  rdblack.Add_ReDir_end(strtmp, str1, nline,NfileNames,curClassification);
               else        rdblack.Add_ReDir_end(strtmp, DefaultRedir,nline,NfileNames,curClassification);

           } else {              // поиск по началу урла
              if(str1[0])  rdblack.Add_ReDir(strtmp, str1, nline,NfileNames,curClassification);
               else        rdblack.Add_ReDir(strtmp, DefaultRedir,nline,NfileNames,curClassification);
           }
        }

     } /* end of while(fgets) */

     fclose(fp);
      if(raz == 0)
      {  fp = fopen(BlackListUpdate,"r");
         if(fp == NULL)
            break;
         printf("read %s   \r",BlackListUpdate); fflush(stdout);
         NfileNames++;
         fileNames[NfileNames] = (char *) malloc(strlen(BlackListUpdate)+2);
         strcpy(fileNames[NfileNames],BlackListUpdate);
         nline = 0;
      } else if(raz == 1 && BlackList2) {

         fp = fopen(BlackList2,"r");
         if(fp == NULL)
         {  printf("Can't open file %s\n",BlackList2);
            sgLogError("Can't open file %s",BlackList2);
              break;
         }
         printf("read %s   \r",BlackList2); fflush(stdout);
         NfileNames++;
         fileNames[NfileNames] = (char *) malloc(strlen(BlackList2)+2);
         strcpy(fileNames[NfileNames],BlackList2);
         nline = 0;
      }

    }
    NfileNames++;

    return 0;
}

/* проверить на предмет - не прошло ли 24 часа после запуска */
/* если да - сбросить логи по клиентам и очистить статистику */
int ReDirMain::CheckForSaveHour(void)
{   time_t t;
    struct tm  *dtm;
    int i, rc;
static int iswork=0;

    rc = 0;
    time(&t);

    dtm = localtime(&t);
    if(lastLogsSaveHour == -1) /* старт */
    {  lastLogsSaveDay = dtm->tm_mday;
       lastLogsSaveHour = dtm->tm_hour;
    }

/* save logs and free statistics */
    if(dtm->tm_hour == lastLogsSaveHour && dtm->tm_mday != lastLogsSaveDay)
    {
        if(iswork)
                return 0;
        iswork = 1;

       while(__lxchg(&semReconfigInProgress, LOCKED))
                                           DosSleep(1); //взведем семафор
        DosBeep(1000,30);  //бикнем
        while(numQueryInProgress)
        {     DosSleep(1); //подождем, пока все запросы не выполнятся
        }

        for(i= 0; i < nusers; i++)
        {  if(user[i].sts)
              user[i].savelog();
        }
        nusers = 0;

       lastLogsSaveDay = dtm->tm_mday;
       lastLogsSaveHour = dtm->tm_hour;
       rc = 1;
       iswork = 0;

       __lxchg(&semReconfigInProgress, UNLOCKED);
    }
    return rc;
}

/* Read usersinfo file */
int ReDirMain::ReadUsersInfo(void)
{  FILE *fp;
   char str[MAX_BUF];
   char uaddr[128],username[128];
   int i,l,is,Waitmin,MaxHits,rc;

   if(UsersInfoFname[0] == 0) return 1;
   fp = fopen(UsersInfoFname,"r");
   if(fp == NULL)
         return 2;
    while(fgets(str,MAX_BUF,fp))
    {
       l = strlen(str);
       is = 0;
       for(i=0;i<l;i++)
       {  if(str[i] > 32)
          {  if(str[i] == '#' || str[i] == ';' ) is = 1;
             break;
          }
       }
       if(is)       /* it is comment */
          continue;
//UserAddress UserName [[Wait_minBlack_List] MaxHitsPerHourBlack_List]
       rc = sscanf(str,"%s %s %i %i",uaddr,username,&Waitmin,&MaxHits);
       if(rc >= 2)
       {  if(rc < 4) MaxHits = BlackListMaxHitsPerHour;
          if(rc < 3) Waitmin = BlackListWait_min;

          rc = AddUser(uaddr, username, MaxHits, Waitmin);
          if(rc < 0)
          {  printf("No memory for users, increase MAX_USERS[%i]",MAX_USERS);
             sgLogError("No memory for users, increase MAX_USERS[%i]",MAX_USERS);
             break;
          }
       }

    }
   fclose(fp);
   return 0;
}

/* добавить узера, и записать его параметры ,   вернуть номер узера */
int ReDirMain::AddUser(char *usersrc, char *username, int maxhit, int maxwait)
{  int rc,id;
   rc = CheckUser(usersrc);
   if(rc < 0)
      return rc;
   id = rc;
   strncpy(user[id].name,username,79);
   user[id].name[79] = 0;
   user[id].BlackListMaxHitsPerHour = maxhit;
   user[id].BlackListWait_min       = maxwait;
   user[id].usePersonalPar = 1;
   return id;
}

/* проверить наличие юзера, если его нет - занести в таблицу,
   вернуть номер узера
*/
int ReDirMain::CheckUser(char *usersrc)
{  int i, is;

   is = 0;
   for(i= 0; i < nusers; i++)
   {  if(user[i].sts)
      {  if(!strcmp(user[i].src,usersrc))
         { is = 1;
           break;
         }
      }
   }
   if(is)
      return i;
/* юзера в таблице еще нет        */
/* найти свободное место для него */
   is = 0;
   for(i= 0; i < nusers; i++)
   {  if(!user[i].sts)
      {  is = 1;
         break;
      }
   }
   if(!is)
   {  if(nusers < MAX_USERS)
      {   i = nusers++;
      } else {
          return -1; // no space for users, increase MAX_USERS
      }
   }
   user[i].InitUser();
   user[i].sts = 1;
   strcpy(user[i].src,usersrc);

   return i;
}

int ReDirCategory::Add_ReDir_redirurl(char *redirurl, int mode)
{  int i,is,l1;
   is = 0;

   for(i=0;i <  rdmain.NredirUrls ;i++)
   {  if(!strcmp(rdmain.pRedirUrl[i],redirurl) )
      { is = 1;
        if(mode == 2)
        {  redir_end[Nredir_end].redirurl = rdmain.pRedirUrl[i];
        } else if(mode == 1) {
           redir_any[Nredir_any].redirurl = rdmain.pRedirUrl[i];
        } else {
           redir2[Nredir].redirurl = rdmain.pRedirUrl[i];
        }
        break;
      }
   }
   if(is) return 2;
   l1 = strlen(redirurl);
   memcpy(&rdmain.RedirUrlBuff[rdmain.L_RedirUrl],redirurl,l1+1);
   rdmain.pRedirUrl[rdmain.NredirUrls] = &rdmain.RedirUrlBuff[rdmain.L_RedirUrl];
   if(mode == 2)
   {  redir_end[Nredir_end].redirurl = rdmain.pRedirUrl[rdmain.NredirUrls];
   } else if(mode == 1) {
      redir_any[Nredir_any].redirurl = rdmain.pRedirUrl[rdmain.NredirUrls];
   } else {
      redir2[Nredir].redirurl = rdmain.pRedirUrl[rdmain.NredirUrls];
   }

   rdmain.NredirUrls++;
   rdmain.L_RedirUrl += l1 +1;
   return 1;
}

/* добавить урл в список поиска по началу урла */
int ReDirCategory::Add_ReDir(char *url, char *redir, int nline, int NfileNames,int classification)
{ int l0,l1;
  if(url == NULL || redir == NULL)
                       return 1;
  url = FilterUrl(url);

 l0 = strlen(url);
  l1 = strlen(redir);
  if(rdmain.L_UrlBuff + l0 +1 >= MAX_URL_BUFF)
  {   printf("increase MAX_URL_BUFF\n");
      sgLogError("increase MAX_URL_BUFF");
      if(redir2)
         free(redir2);
      exit(1);
  }
  if(rdmain.L_RedirUrl + l1 +1 >= MAX_REDIR_URL_BUFF)
  {   printf("increase MAX_REDIR_URL_BUFF\n");
      sgLogError("increase MAX_REDIR_URL_BUFF");
      if(redir2)
         free(redir2);
      exit(1);
  }

  if(Nredir+ 1 >= nA_redir2)
  {
      if(nA_redir2 < 512) nA_redir2 = 512;
      else                nA_redir2 += nA_redir2/4;
      if(redir2)
      {     redir2 = (struct Eredir2 *)
                        realloc((void *)redir2, nA_redir2 * sizeof(struct Eredir2) );
      } else {
            redir2 = (struct Eredir2 *) calloc( nA_redir2, sizeof(struct Eredir2) );
      }
      if(redir2 == NULL)
      {  printf("Error alloc memory at %i %s\n",__LINE__, __FILE__);
         sgLogError("Error alloc memory at %i %s\n",__LINE__, __FILE__);
         exit(1);
      }
  }
  memcpy(&rdmain.UrlBuff[rdmain.L_UrlBuff],url,l0+1);
  redir2[Nredir].url = &rdmain.UrlBuff[rdmain.L_UrlBuff];
  rdmain.L_UrlBuff += l0 +1;
  redir2[Nredir].line = nline;
  redir2[Nredir].section = NfileNames;

  redir2[Nredir].type = 0;
  redir2[Nredir].classification = classification;
  Add_ReDir_redirurl(redir,0);
  Nredir++;
  return 0;
}

/* добавить урл в список абортов  по началу урла */
int ReDirCategory::Add_ReDir_abort(char *url, char *redir, int nline, int NfileNames, int classification)
{ int l0,l1;
  if(url == NULL || redir == NULL)
                       return 1;
  if(*url == '!') url++;

 //надо ли ???  url = FilterUrl(url);

    if(!strncmp(url,sg_strhttp, sizeof(sg_strhttp)-1) ) // убираем http://
    {  url += sizeof(sg_strhttp) - 1;
    }
    if(!strncmp(url,sg_strwwwdot, strlen(sg_strwwwdot)) )   //убираем www.
    {  url += strlen(sg_strwwwdot);
    }

 l0 = strlen(url);
  l1 = strlen(redir);
  if(rdmain.L_UrlBuff + l0 +1 >= MAX_URL_BUFF)
  {   printf("increase MAX_URL_BUFF\n");
      sgLogError("increase MAX_URL_BUFF");
      exit(1);
  }
  if(rdmain.L_RedirUrl + l1 +1 >= MAX_REDIR_URL_BUFF)
  {   printf("increase MAX_REDIR_URL_BUFF\n");
      sgLogError("increase MAX_REDIR_URL_BUFF");
      exit(1);
  }

  if(Nredir_abort+ 1 >= nA_redir_abort)
  {
      if(nA_redir_abort < 512) nA_redir_abort = 512;
      else                nA_redir_abort += nA_redir_abort/4;
      if(redir_abort)
      {     redir_abort = (struct Eredir2 *)
                        realloc((void *)redir_abort, nA_redir_abort * sizeof(struct Eredir2) );
      } else {
            redir_abort = (struct Eredir2 *) calloc( nA_redir_abort, sizeof(struct Eredir2) );
      }
      if(redir_abort == NULL)
      {  printf("Error alloc memory at %i %s\n",__LINE__, __FILE__);
         sgLogError("Error alloc memory at %i %s\n",__LINE__, __FILE__);
         exit(1);
      }
  }
  memcpy(&rdmain.UrlBuff[rdmain.L_UrlBuff],url,l0+1);
  redir_abort[Nredir_abort].url = &rdmain.UrlBuff[rdmain.L_UrlBuff];
  rdmain.L_UrlBuff += l0 +1;
  redir_abort[Nredir_abort].line = nline;
  redir_abort[Nredir_abort].section = NfileNames;

  redir_abort[Nredir_abort].type = 0;
  redir_abort[Nredir_abort].classification = classification;

  //Add_ReDir_redirurl(redir,3); /* no any redirect */
  Nredir_abort++;
  return 0;
}


/* добавить урл в список поиска по конца урла */
int ReDirCategory::Add_ReDir_end(char *url, char *redir, int nline, int NfileNames, int classification)
{ int l0,l1,i;

  if(url == NULL || redir == NULL)
                       return 1;
  l0 = strlen(url);
  if(url[l0-1] == '$') l0--;
  l1 = strlen(redir);
  if(rdmain.L_UrlBuff + l0 +1 >= MAX_URL_BUFF)
  {   printf("increase MAX_URL_BUFF\n");
      sgLogError("increase MAX_URL_BUFF");
      exit(1);
  }
  if(rdmain.L_RedirUrl + l1 +1 >= MAX_REDIR_URL_BUFF)
  {   printf("increase MAX_REDIR_URL_BUFF\n");
      sgLogError("increase MAX_REDIR_URL_BUFF");
      exit(1);
  }

  if(Nredir_end + 1 >= nA_redir_end)
  {
      if(nA_redir_end < 512) nA_redir_end = 512;
      else                   nA_redir_end += nA_redir_end/4;
      if(redir2)
      {    redir_end = (struct Eredir2 *)
                         realloc((void *)redir_end, nA_redir_end * sizeof(struct Eredir2) );
      } else {
            redir_end = (struct Eredir2 *) calloc( nA_redir_end, sizeof(struct Eredir2) );
      }
      if(redir_end == NULL)
      {  printf("Error alloc memory at %i\n",__LINE__);
         sgLogError("Error alloc memory at %i\n",__LINE__);
         exit(1);
      }
  }

  for(i=0;i<l0;i++)
  {  rdmain.UrlBuff[rdmain.L_UrlBuff+i] = url[l0-i-1];
  }
  rdmain.UrlBuff[rdmain.L_UrlBuff+l0] = 0;

  redir_end[Nredir_end].url = &rdmain.UrlBuff[rdmain.L_UrlBuff];
  rdmain.L_UrlBuff += l0 +1;
  redir_end[Nredir_end].line = nline;
  redir_end[Nredir_end].section = NfileNames;

  redir_end[Nredir_end].type = 2;
  redir_end[Nredir_end].classification = classification;
  Add_ReDir_redirurl(redir,2);
  Nredir_end++;
  return 0;
}

/* добавить урл в список поиска по подстроке урла */
int ReDirCategory::Add_ReDir_any(char *url, char *redir, int nline, int NfileNames, int classification)
{ int l0,l1;
  if(url == NULL || redir == NULL)
                       return 1;

  if(*url == '*') url++;

  l0 = strlen(url);
  l1 = strlen(redir);
  if(rdmain.L_UrlBuff + l0 +1 >= MAX_URL_BUFF)
  {   printf("increase MAX_URL_BUFF\n");
      sgLogError("increase MAX_URL_BUFF");
      exit(1);
  }
  if(rdmain.L_RedirUrl + l1 +1 >= MAX_REDIR_URL_BUFF)
  {   printf("increase MAX_REDIR_URL_BUFF\n");
      sgLogError("increase MAX_REDIR_URL_BUFF");
      exit(1);
  }

  if(Nredir_any+ 1 >= MAX_REDIR_URL_BUFF)
  {   printf("increase MAX_REDIR_TABLE (for \"any\")\n");
      sgLogError("increase MAX_REDIR_TABLE (for \"any\")");
      exit(1);
  }
  if(Nredir_any + 1 >= nA_redir_any)
  {
      if(nA_redir_any < 512) nA_redir_any = 512;
      else                   nA_redir_any += nA_redir_any/4;

      if(redir2)
      {     redir_any = (struct Eredir2 *)
                               realloc((void *)redir_any, nA_redir_any * sizeof(struct Eredir2) );
      } else {
            redir_any = (struct Eredir2 *) calloc( nA_redir_any, sizeof(struct Eredir2) );
      }
      if(redir_any == NULL)
      {  printf("Error alloc memory at %i\n",__LINE__);
         sgLogError("Error alloc memory at %i\n",__LINE__);
         exit(1);
      }
  }

  memcpy(&rdmain.UrlBuff[rdmain.L_UrlBuff],url,l0+1);
  redir_any[Nredir_any].url = &rdmain.UrlBuff[rdmain.L_UrlBuff];
  rdmain.L_UrlBuff += l0 +1;
  redir_any[Nredir_any].line = nline;
  redir_any[Nredir_any].section = NfileNames;
  redir_any[Nredir_any].type = 1;
  redir_any[Nredir_any].classification = classification;

  Add_ReDir_redirurl(redir,1);
  Nredir_any++;
 return 0;
}


/* чтение строки из сокета */
/*
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
*/
/*
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
*/

/* функция сравнения struct Eredir2 */
int _Optlink RedirSortCmp(const void* s1, const void* s2)
{   struct Eredir2 * pvInd1, *pvInd2;
    int rc;

    pvInd1 =(struct Eredir2 *) s1;
    pvInd2 =(struct Eredir2 *) s2;
    rc = strcmp(pvInd1->url,pvInd2->url);
    return rc;
}

int ReDirCategory::ReDir_SortIndex(void)
{   int rc, nold1,nold2,nold3;
    printf("sorting...\r"); fflush(stdout);
    qsort( (void *) redir2,   Nredir, sizeof(struct Eredir2), RedirSortCmp);
    qsort( (void *) redir_any,Nredir_any, sizeof(struct Eredir2), RedirSortCmp);
    qsort( (void *) redir_end,Nredir_end, sizeof(struct Eredir2), RedirSortCmp);
    qsort( (void *) redir_abort,Nredir_abort, sizeof(struct Eredir2), RedirSortCmp);

    printf("Delete dups: "); fflush(stdout);
    nold1 = Nredir;
    DelDuplicity(redir2, Nredir);
    nold2 = Nredir_end;
    DelDuplicity(redir_end, Nredir_end);
    nold3 = Nredir_abort;
    DelDuplicity(redir_abort, Nredir_abort);
    printf("Bn=%i->%i, En%i->%i, An%i->%i\n",nold1,Nredir,nold2,Nredir_end,nold3, Nredir_abort);
    fflush(stdout);

    return 0;
}
/* удаляем повторения, которые приводят к промахам, напр */
/* при наличии в базе
   pantyhose.
   pantyhose.st
   и запросе pantyhose.sk двоичный поиск выдаст pantyhose.st и получится промах
   вместо  pantyhose.
   имеет смысл для правил с поиском по началу, с поиском по концу
   (т.к. строка инвертируется);
*/
int DelDuplicity(struct Eredir2 *red, int &Nred)
{  int i,j,n,nn,l,l1, shift, is;
   char *purl, *purl1;
   struct Eredir2 tmp;
   struct Eredir2 *redv;
   if(Nred <= 0)
      return Nred;
   n = Nred;
   redv = &red[0];

/* test for pure doubbling */
   nn = 1;
   for(i=1;i<n;i++)
   {  purl1 = red[i].url;
      purl  = redv->url;

      if(strcmp(purl,purl1) )
      {   redv++;
          *redv = red[i];
          nn++;
      } else {
         if(red[i].section < redv->section)
         { tmp = *redv;
           *redv = red[i];
           red[i] = tmp;
         }
      }
   }
   n = nn;

   for(i=0;i<n-1;i++)
   {  purl = red[i].url;
      purl1= red[i+1].url;


      l  = strlen(purl);
/* test for pure ip addr  i.e. [0-9] and '.' */
      for(is=j=0;j<l;j++)
      {  if(purl[j] != '.' && !(int(purl[j]) >= int('0') && int(purl[j]) <= int('9')) )
         {  is=1;
            break;
         }
      }
      if(!is)
      {   continue;
      }

      l1 = strlen(purl1);
      if(l1 <= l) continue;
      if(strncmp(purl,purl1,l) )
                     continue;
      shift=1;
      for(j=i+2; j<n-1; j++)
      {
         purl1= red[j].url;
         l1 = strlen(purl1);
         if(l1 <= l)
                      break;
         if(strncmp(purl,purl1,l) )
                      break;
         shift++;
      }
/* purl1 совпадает началом с purl и длинее => надо убрать */
      for(j=i+1; j< n-shift; j++)  red[j] = red[j+shift];
/* уменьшаем количество элементов */
      n -= shift;
//      if(!(i%100)) printf("%i \r",n); fflush(stdout);
/* повторяем для того же i */
      i--;

   }
   Nred = n;
   return n;
}

/* двоичный поиск наличия начала урла в индексе  */
int ReDirCategory::RedirSearch(struct Eredir2 redir[],int _nred,char *url, int *Error)
{
    int low, high, mid,n, rc,lk,l,ii,i;
    struct Eredir2 key;
    char *urldot;
  for(ii=0;ii<2;ii++)
  {
    *Error = 1;
    key.url = url;
    key.line=0; key.type = 0; key.section = 0;
    low  = 0;
    n = _nred;
    if(n <= 0)
            return -1;
    high = n -1;
    while(low <= high)
    {   mid = (low + high) /2;
        rc = RedirSortCmp(&redir[mid], &key);
        if(rc > 0)
               high = mid - 1;
        else
          if( rc < 0 )
               low = mid + 1;
        else
        { *Error = 0;
          return mid; /* found */
        }
    }

    rc = RedirSortCmp(&redir[mid], &key);
    if(rc > 0 && mid)
    {            mid--;
          l =  strlen(redir[mid].url);
          lk = strlen(url);
          if(lk > l)
          {  if(!strncmp(redir[mid].url,url,l) )
                                        *Error = 0;
          }
    } else
         if(rc < 0 && mid < n) {

          l =  strlen(redir[mid].url);
          lk = strlen(url);
          if(lk > l)
          {  if(!strncmp(redir[mid].url,url,l) )
                                        *Error = 0;
          }
    }
    if(*Error == 0)
           break;
    else if(!ii) /* only 1 sub-domain */
    {  int is=0;
       urldot = NULL;
       l = strlen(url);
       for(i=0;i<l;i++)
       {   if(url[i] == '/') break;
           if(url[i] == '\\') break;
           if(url[i] == '.')
           {  if(is++)
                   break;

              urldot = &url[i+1];
           }
       }
       if(is == 2) url = urldot;
       else
           break;
    }
  } /* end for */
    return mid;
}

/* двоичный поиск наличия конца урла в индексе  */
int ReDirCategory::RedirSearch_end(char *url, int *Error)
{
    int low, high, mid,n, rc,lk,l,i,l0;
    struct Eredir2 key;
static  char turl[MAX_BUF];

/* для начала обратим url */
   l0 = strlen(url);
   for(i=0;i<l0;i++)
   {  turl[i] = url[l0-i-1];
   }
   turl[l0] = 0;

    *Error = 0;
    key.url = &turl[0];
    key.line=0; key.type = 0; key.section = 0;
    low  = 0;
    n = Nredir_end;
    if(n <= 0)
         return -1;
    high = n -1;
    while(low <= high)
    {   mid = (low + high) /2;
        rc = RedirSortCmp(&redir_end[mid], &key);
        if(rc > 0)
               high = mid - 1;
        else
          if( rc < 0 )
               low = mid + 1;
        else
          return mid; /* found */
    }
    *Error = 1;
//printf("MM4 mid = %i, %s %s\n",mid, rd.redir_end[mid].url,key.url);
    rc = RedirSortCmp(&redir_end[mid], &key);
//printf("MM5\n");
    if(rc > 0 && mid)
    {            mid--;
          l =  strlen(redir_end[mid].url);
          lk = strlen(turl);
          if(lk > l)
          {  if(!strncmp(redir_end[mid].url,turl,l) )
                                        *Error = 0;
          }
    } else
         if(rc < 0 && mid < n) {

          l =  strlen(redir_end[mid].url);
          lk = strlen(url);
          if(lk > l)
          {  if(!strncmp(redir_end[mid].url,turl,l) )
                                        *Error = 0;
          }
    }
    return mid;
}


/* поиск перебором наличия подстроки в  урле */
int ReDirCategory::RedirSearch_any(char *url, int *Error)
{
    int i, rc;

    *Error = 1;

    for(i=0;i<Nredir_any;i++)
    {  if(strstr(url,redir_any[i].url) )
       {  *Error = 0;
          return i;
       }
    }
    return -1;
}

/* обработка запроса анализатора */
int HandleAnalyzerQuery(char *buf, char *bufout, int nclient,int &Classification, int &redircode)
{
   struct SquidInfo squidInfo;
   char *redirect;
   struct Source *src;
   char redirBuff[MAX_BUF];
   int rc =0;

    while(semReconfigInProgress)
                       DosSleep(1);

    if( sgCheckForReloadConfig(configFile,CHECK_CONFIG_TIME) )
    {
       while(__lxchg(&semReconfigInProgress, LOCKED))
                                           DosSleep(1); //взведем семафор
        DosBeep(1000,30);  //бикнем
        while(numQueryInProgress)
        {     DosSleep(1); //подождем, пока все запросы не выполнятся
           if(!detachedMode)
            {     printf("Reconf1 %i",numQueryInProgress);
                  fflush(stdout);
            }
        }
        rdmain.sgReadConfig(configFile);
       __lxchg(&semReconfigInProgress, UNLOCKED);
    }

   rc = __lxchg(&semnumQueryInProgress, LOCKED);
   if(rc)
   {
 printf("AddToQueue %i\n",nclient);
        AddToQueue(nclient);
        do
        {  while(__lxchg(&semnumQueryInProgress, LOCKED)) DosSleep(1);

        } while(ReadQueue(nclient));
   }

   numQueryInProgress++;
   __lxchg(&semnumQueryInProgress, UNLOCKED);

    strcpy(squidInfo.url,buf);
    strcpy(squidInfo.orig,buf);

    if((redirect = MainRedir(src,&squidInfo,redirBuff,nclient,0,Classification, redircode)) == NULL)
    {
        rc = 1;
    } else {

    }
RET:
   while(__lxchg(&semnumQueryInProgress, LOCKED)) DosSleep(1);
   numQueryInProgress--;
   __lxchg(&semnumQueryInProgress, UNLOCKED);

    return rc;

}
//DosCreateQueue(

int HandleClientQuery(char *buf, char *bufout, int nclient)
{
   struct SquidInfo squidInfo;
   char *redirect;
   struct Source *src;
   char redirBuff[MAX_BUF];
   int rc =0,Classification, redircode;

    if(buf == NULL || bufout == NULL)
                              return -1;
    while(semReconfigInProgress)
                       DosSleep(1);

    if( sgCheckForReloadConfig(configFile,CHECK_CONFIG_TIME) )
    {
       while(__lxchg(&semReconfigInProgress, LOCKED))
                                           DosSleep(1); //взведем семафор
        DosBeep(1000,30);  //бикнем
        while(numQueryInProgress)
        {     DosSleep(1); //подождем, пока все запросы не выполнятся
           if(!detachedMode)
            {     printf("Reconf1 %i",numQueryInProgress);
                  fflush(stdout);
            }
        }
        rdmain.sgReadConfig(configFile);
       __lxchg(&semReconfigInProgress, UNLOCKED);
    }


   while(__lxchg(&semnumQueryInProgress, LOCKED)) DosSleep(1);
   numQueryInProgress++;
   __lxchg(&semnumQueryInProgress, UNLOCKED);

   rdmain.CheckForSaveHour();

    *bufout = 0;

    if(parseLine(buf,&squidInfo) != 1)
    {  sgLogError("error parsing squid line: %s",buf);
       rc = 1;
       goto RET;
    }


    if((redirect = MainRedir(src,&squidInfo,redirBuff,nclient,1,Classification, redircode)) == NULL)
    {
//      printf("%sЖ\n",buf);
//      sg_puts("", fd);
        rc = 1;
    } else {
          if(squidInfo.srcDomain[0] == '\0')
          { squidInfo.srcDomain[0] = '-';
            squidInfo.srcDomain[1] = '\0';
          }
          if(squidInfo.ident[0] == '\0')
          { squidInfo.ident[0] = '-';
            squidInfo.ident[1] = '\0';
          }

         sprintf(bufout,"%s %s/%s %s %s",redirect,squidInfo.src,
                 squidInfo.srcDomain,squidInfo.ident,
                 squidInfo.method);
//         sg_puts(buf, fd);
      }
RET:
   while(__lxchg(&semnumQueryInProgress, LOCKED)) DosSleep(1);
   numQueryInProgress--;
   __lxchg(&semnumQueryInProgress, UNLOCKED);

    return rc;
}

/* проверка правил для категории        */
/* возвращает номер типа редирекиции    */
/* в rind - индекс сработавшего правила */
int ReDirCategory::RedirCheck(int iduser, char *purl, char *redirBuff,
                               char *orig, char * * pcrc, int *rid)
{   int i,j,rc,err, rcret=0;
    int t1,t0,dt, rcfplog,ch,ch1;

    rc = RedirSearch(redir_abort, Nredir_abort, purl, &err);
    if(err == 0)
    {
         return rcret;    /* Abort rule */
    }
    rc = RedirSearch(redir2, Nredir, purl, &err);
    if(err == 0)
    {
        redirBuff[0]=0;
        if(!strstr(redir2[rc].redirurl,sg_strhttp) )
        {   strcpy(redirBuff, rdmain.BaseRedirUrl);
            strcat(redirBuff,"/");
        }
       strcat(redirBuff,redir2[rc].redirurl);
       *pcrc = redirBuff;
       *rid = rc;
       dt = t1-t0;
       rcret = 1;
    } else {
       rc =  RedirSearch_any(purl, &err);
       if(rc >= 0)
       {
          redirBuff[0]=0;
          if(!strstr(redir_any[rc].redirurl,sg_strhttp) )
          {   strcpy(redirBuff, rdmain.BaseRedirUrl);
              strcat(redirBuff,"/");
          }
         strcat(redirBuff,redir_any[rc].redirurl);
         *pcrc = redirBuff;
         *rid = rc;
         rcret = 2;

       } else {
           rc =  RedirSearch_end(purl, &err);
           if(rc >= 0 && err == 0)
           {   redirBuff[0]=0;
               if(!strstr(redir_end[rc].redirurl,sg_strhttp) )
               { strcpy(redirBuff, rdmain.BaseRedirUrl);
                 strcat(redirBuff,"/");
               }
                strcat(redirBuff,redir_end[rc].redirurl);
                *rid = rc;
                *pcrc = redirBuff;
                rcret = 3;
           }
/* no redirection */
       }
    }
    return rcret;
}

char *MainRedir(struct Source *src, struct SquidInfo *squidInfo, char *redirBuff,
                   int globalPid, int checkUser,int &Classification, int &redircode)
{   int i,j,id,rc=0,err;
    FILE *fp;
    char *purl, *pcrc=NULL;
    int t1,t0,dt, rcfplog,ch,ch1,ch2;
    int iduser=0;
    char str[80];
    char strTmp[MAX_BUF+16];

    if(checkUser)
    {
      iduser = rdmain.CheckUser(squidInfo->src);
      if(iduser == -1)
      {      sgLogError("no space for users, increase MAX_USERS > %i, Exit",  MAX_USERS);
             exit(2);
      }
      rc = user[iduser].CheckUserForBanned( rdmain.BlackListMaxHitsPerHour,rdmain.BlackListWait_min);
      if(rc) //banned
      {   char *pUserinf = user[iduser].src;
          if(user[iduser].name[0]) pUserinf = user[iduser].name;

           redirBuff[0]=0;
          if(!strstr(rdmain.BlackListWaitUrl[rdmain.iBlackListWaitUrl],sg_strhttp) )
          {   strcpy(redirBuff, rdmain.BaseRedirUrl);
              strcat(redirBuff,"/");
          }
          strcat(redirBuff,rdmain.BlackListWaitUrl[rdmain.iBlackListWaitUrl]);
          rdmain.iBlackListWaitUrl = (rdmain.iBlackListWaitUrl+1) % rdmain.nBlackListWaitUrl;
          pcrc = redirBuff;
          if(rc == 1)
          { rcfplog = fprintf(rdmain.fplog,"X %s \n", pUserinf);
            DosBeep(500,20);  //бикнем
            sgLogError("User % s banned", pUserinf);
          }
          if(!detachedMode)
          {
              printf(PORNO_REDIR_COLOR "XXX %s \n" NORMAL_COLOR,pUserinf);
          }
          redircode = -1;
          return pcrc;
      }
    }

/*
    fprintf(fp,"Prot %s,",squidInfo->protocol);
    fprintf(fp,"Domain:%s,",squidInfo->domain);
    fprintf(fp,"Dot:%i,",squidInfo->dot);
    fprintf(fp,"Url:%s,",squidInfo->url);
    fprintf(fp,"Orig:%s,",squidInfo->orig);
    fprintf(fp,"Surl:%s,",squidInfo->surl);
    fprintf(fp,"Strippedurl:%s,",squidInfo->strippedurl);
    fprintf(fp,"Port:%i,",squidInfo->port);
    fprintf(fp,"Src:%s,",squidInfo->src);
    fprintf(fp,"SrcDomain:%s,",squidInfo->srcDomain);
    fprintf(fp,"Ident:%s,",squidInfo->ident);
    fprintf(fp,"Method:%s\n",squidInfo->method);

*/
    t0 = clock();
    purl = squidInfo->url;
    strTmp[0] = 0;
/************************************************/
    purl = FilterUrl(purl);
/************************************************/
    rc = rdmain.rd.RedirCheck(iduser,purl,redirBuff, squidInfo->orig,&pcrc,&id);
    if(rc)
    {
       t1 = clock();
       dt = t1-t0;
       if(checkUser) user[iduser].AddHit(1,globalPid);
       strncpy(str,squidInfo->orig,78); str[79] = 0;

        switch(rc)
           {
               case 1:
                  redircode = 1;
                  Classification = rdmain.rd.redir2[id].classification;
                  if(checkUser)
                  {  rcfplog = fprintf(rdmain.fplog,"B %s  %s  %s %i (%i,%i,%i)\n",squidInfo->orig,
                       rdmain.rd.redir2[id].redirurl,user[iduser].src,dt,rdmain.rd.redir2[id].line,
                       rdmain.rd.redir2[id].section,Classification);
                     if(!detachedMode)
                     {    if(dt)
                          {  sprintf(strTmp, BEGIN_REDIR_COLOR "B%i%i %s %s (%i,%i,%i)" NORMAL_COLOR,
                                   globalPid,dt,str,
                             rdmain.rd.redir2[id].redirurl,rdmain.rd.redir2[id].line,
                             rdmain.rd.redir2[id].section,Classification);
                          } else {
                             sprintf(strTmp, BEGIN_REDIR_COLOR "B%i %s %s (%i,%i,%i)" NORMAL_COLOR,
                                   globalPid,str,
                             rdmain.rd.redir2[id].redirurl,rdmain.rd.redir2[id].line,
                             rdmain.rd.redir2[id].section,Classification);
                          }
                     }
                  }
                   break;
                case 2:
                  redircode = 2;
                  Classification = rdmain.rd.redir_any[id].classification;
                  if(checkUser)
                  {  rcfplog = fprintf(rdmain.fplog,"S %s  %s  %s %i (%i,%i,%i)\n",squidInfo->orig,
                            rdmain.rd.redir_any[id].redirurl,user[iduser].src,dt,
                            rdmain.rd.redir_any[id].line,rdmain.rd.redir_any[id].section,Classification);
                     if(!detachedMode)
                     {  if(dt)
                        {  sprintf(strTmp, ANY_REDIR_COLOR "S%i%i %s %s (%i,%i,%i)" NORMAL_COLOR,
                                  globalPid,dt,str,
                                rdmain.rd.redir_any[id].redirurl, rdmain.rd.redir_any[id].line,
                                rdmain.rd.redir_any[id].section,Classification);
                        } else {
                           sprintf(strTmp, ANY_REDIR_COLOR "S%i %s %s (%i,%i,%i)" NORMAL_COLOR,
                                  globalPid,str,
                                rdmain.rd.redir_any[id].redirurl, rdmain.rd.redir_any[id].line,
                                rdmain.rd.redir_any[id].section,Classification);
                        }
                     }
                   }
                   break;
                case 3:
                   redircode = 3;
                   Classification = rdmain.rd.redir_end[id].classification;
                   if(checkUser)
                   {  rcfplog = fprintf(rdmain.fplog,"E %s  %s  %s %i (%i,%i,%i)\n",squidInfo->orig,
                                rdmain.rd.redir_end[id].redirurl,user[iduser].src,dt,
                                rdmain.rd.redir_end[id].line,rdmain.rd.redir_end[id].section,Classification);
                      if(!detachedMode)
                      {
                        if(dt)
                        {  sprintf(strTmp, END_REDIR_COLOR "E%i%i %s %s (%i,%i,%i)" NORMAL_COLOR,
                                  globalPid,dt,str,
                             rdmain.rd.redir_end[id].redirurl, rdmain.rd.redir_end[id].line,
                             rdmain.rd.redir_end[id].section,Classification);
                        } else {
                           sprintf(strTmp, END_REDIR_COLOR "E%i %s %s (%i,%i,%i)" NORMAL_COLOR,
                                  globalPid,str,
                             rdmain.rd.redir_end[id].redirurl, rdmain.rd.redir_end[id].line,
                             rdmain.rd.redir_end[id].section,Classification);
                        }
                      }
                   }
                   break;
          }  /* endof switch()*/


    } else {
        rc = rdmain.rdblack.RedirCheck(iduser,purl,redirBuff, squidInfo->orig,&pcrc,&id);
        if(rc)
        {  t1 = clock();
           dt = t1-t0;
           strncpy(str,squidInfo->orig,78); str[79] = 0;
           if(checkUser) user[iduser].AddHit(2,globalPid);

           switch(rc)
           {
               case 1:
                  redircode = 0x11;
                  Classification = rdmain.rdblack.redir2[id].classification;
                  if(checkUser)
                  { rcfplog = fprintf(rdmain.fplog,"B %s  %s  %s %i (%i,%i,%i)\n",squidInfo->orig,
                         rdmain.rdblack.redir2[id].redirurl,user[iduser].src,dt,rdmain.rdblack.redir2[id].line,
                         rdmain.rdblack.redir2[id].section,Classification);
                    if(!detachedMode && checkUser)
                    {   if(dt)
                        {  sprintf(strTmp, BEGIN_REDIR_COLOR "B%i%i" PORNO_REDIR_COLOR "%s %s (%i,%i,%i)" NORMAL_COLOR,
                                  globalPid,dt,str,
                             rdmain.rdblack.redir2[id].redirurl,rdmain.rdblack.redir2[id].line,
                             rdmain.rdblack.redir2[id].section,Classification);
                        } else {
                           sprintf(strTmp, BEGIN_REDIR_COLOR "B%i" PORNO_REDIR_COLOR "%s %s (%i,%i,%i)" NORMAL_COLOR,
                                  globalPid,str,
                             rdmain.rdblack.redir2[id].redirurl,rdmain.rdblack.redir2[id].line,
                             rdmain.rdblack.redir2[id].section,Classification);
                        }
                    }
                  }
                   break;
                case 2:
                  redircode = 0x12;
                  Classification = rdmain.rdblack.redir_any[id].classification;
                  if(checkUser)
                  {  rcfplog = fprintf(rdmain.fplog,"S %s  %s  %s %i (%i,%i,%i)\n",squidInfo->orig,
                            rdmain.rdblack.redir_any[id].redirurl,user[iduser].src,dt,rdmain.rdblack.redir_any[id].line,
                            rdmain.rdblack.redir_any[id].section,Classification);
                     if(!detachedMode && checkUser)
                     {  printf(ANY_REDIR_COLOR "S%i",globalPid);
                        if(dt)  printf("%i",dt);
                         printf(PORNO_REDIR_COLOR " %s %s (%i,%i,%i)\n" NORMAL_COLOR,str,
                                rdmain.rdblack.redir_any[id].redirurl, rdmain.rdblack.redir_any[id].line,
                                rdmain.rdblack.redir_any[id].section,Classification);

                        if(dt)
                        {  sprintf(strTmp, ANY_REDIR_COLOR "S%i%i" PORNO_REDIR_COLOR "%s %s (%i,%i,%i)" NORMAL_COLOR,
                                  globalPid,dt,str,
                                rdmain.rdblack.redir_any[id].redirurl, rdmain.rdblack.redir_any[id].line,
                                rdmain.rdblack.redir_any[id].section,Classification);
                        } else {
                           sprintf(strTmp, ANY_REDIR_COLOR "S%i" PORNO_REDIR_COLOR "%s %s (%i,%i,%i)" NORMAL_COLOR,
                                  globalPid,str,
                                rdmain.rdblack.redir_any[id].redirurl, rdmain.rdblack.redir_any[id].line,
                                rdmain.rdblack.redir_any[id].section,Classification);
                        }

                     }
                   }
                   break;
                case 3:
                  redircode = 0x13;
                  Classification = rdmain.rdblack.redir_end[id].classification;
                  if(checkUser)
                  { rcfplog = fprintf(rdmain.fplog,"E %s  %s  %s %i (%i,%i,%i)\n",squidInfo->orig,
                                rdmain.rdblack.redir_end[id].redirurl,user[iduser].src,dt, rdmain.rdblack.redir_end[id].line,
                                rdmain.rdblack.redir_end[id].section,Classification);
                     if(!detachedMode)
                     {  if(dt)
                        {  sprintf(strTmp, END_REDIR_COLOR "E%i%i" PORNO_REDIR_COLOR "%s %s (%i,%i,%i)" NORMAL_COLOR,
                                  globalPid,dt,str,
                             rdmain.rdblack.redir_end[id].redirurl, rdmain.rdblack.redir_end[id].line,
                             rdmain.rdblack.redir_end[id].section,Classification);
                        } else {
                           sprintf(strTmp, END_REDIR_COLOR "E%i" PORNO_REDIR_COLOR "%s %s (%i,%i,%i)" NORMAL_COLOR,
                                  globalPid,str,
                             rdmain.rdblack.redir_end[id].redirurl, rdmain.rdblack.redir_end[id].line,
                             rdmain.rdblack.redir_end[id].section,Classification);
                        }

                     }
                  }
                   break;
          }  /* endof switch()*/


        } else {
/* no redirection */
           t1 = clock();
           dt = t1-t0;
           if(checkUser)
           {   user[iduser].AddHit(0,globalPid);
               rcfplog =  fprintf(rdmain.fplog,"n %s %i\n",squidInfo->orig,dt);
               if(rcfplog== -1)
                  sgLogError("write to fplog failed, (fplog=%p)",rdmain.fplog);
               if(!detachedMode)
               {     if(dt) /* ?? NORMAL_COLOR */
                     {  sprintf(strTmp, "n%i%i %s",
                                  globalPid,dt,squidInfo->orig);
                     } else {
                       sprintf(strTmp, "n%i %s",
                                  globalPid,squidInfo->orig);
                     }
               }
           }
           redircode = 0x0;
           Classification  = 0;
        }

    }

   if(!detachedMode)
   { if(strTmp[0])
     {  puts(strTmp);
     }
/*  fflush(stdout); */
   }
    return pcrc;
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

/* filter source ulr:
   del "http://"
   del "www." if any
   del "www[1-9]." if any
   conv "ad[1-99]." -> "ad."
   conv "adv[1-99]." -> "adv."
*/
char * FilterUrl(char * purl)
{   int i,j,ch,ch1;

/*********************************************************/
    if(!strncmp(purl,sg_strhttp, sizeof(sg_strhttp) - 1) )  // del http://
    {  purl += sizeof(sg_strhttp) - 1 ;
    }
    if(!strncmp(purl,sg_strwwwdot, sizeof(sg_strwwwdot)-1) ) // del www. if any
    {  purl += sizeof(sg_strwwwdot)-1;
    } else
       if(!strncmp(purl,sg_strwww, sizeof(sg_strwww)-1) )   // check for www[1-9]
    {   i = sizeof(sg_strwww)-1;
        ch  = (int) purl[i];
        ch1 = (int) purl[i+1];
        if(ch1 == (int) '.' && ch >= (int)'1'  && ch <= (int) '9' )
                                     purl += i + 2;
    }  else
       if(!strncmp(purl,sg_strad, sizeof(sg_strad)-1) )   // check for ad[1-99]
    {   int isad=0;
        i = sizeof(sg_strad)-1;
        ch  = (int) purl[i];
        ch1 = (int) purl[i+1];
        if(ch1 == (int) '.' && ch >= (int)'0'  && ch <= (int) '9' ) isad = 1;
        else if(((int) purl[i+2] == (int) '.') && ch >= (int)'1'  && ch <= (int) '9'
                                               && ch1 >= (int)'0' && ch1 <= (int) '9') isad = 2;
        if(isad )
        {    for(j=0;j<i;j++)
             {   purl[j + isad] = sg_strad[j];
             }
             purl += isad;
        }
    } else
       if(!strncmp(purl,sg_stradv,sizeof(sg_stradv)-1) )   // check for adv[1-99]
    {   int isad=0;
        i = sizeof(sg_stradv)-1;
        ch  = (int) purl[i];
        ch1 = (int) purl[i+1];
        if(ch1 == (int) '.' && ch >= (int)'0'  && ch <= (int) '9' ) isad = 1;
        else if(((int) purl[i+2] == (int) '.') && ch >= (int)'1'  && ch <= (int) '9'
                                               && ch1 >= (int)'0' && ch1 <= (int) '9') isad = 2;
        if(isad )
        {    for(j=0;j<i;j++)
             {   purl[j + isad] = sg_stradv[j];
             }
             purl += isad;
        }
    }

/************************************************/
    return purl;
}

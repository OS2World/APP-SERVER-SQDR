/* SQDRclient.h */
/* class NPipe, связь клиента с сервером */
#define SQDR_BASE_PIPE_NAME        "\\PIPE\\SQDR"
#define MAX_NUM_PIPES 32

#define SERVER_MODE              1
#define CLIENT_MODE              2
#define SERVER_COLOR             "[0;32;40m"
#define CLIENT_COLOR             "[0;36;40m"
#define NORMAL_COLOR             "[0;37;40m"
#define BEGIN_REDIR_COLOR        "[1;37;40m"
#define END_REDIR_COLOR          "[1;32;40m"
#define ANY_REDIR_COLOR          "[1;33;40m"
#define PORNO_REDIR_COLOR        "[1;31;40m"

/*
 Десятичное число, указывающее одну из заданных ниже функций.
 Вы можете задать несколько функций, разделяя их запятыми.

  Цвет      Описание

            Атрибуты текста
  0         Отменить все атрибуты
  1         Повышенная яркость
  2         Пониженная яркость
  3         Курсив
  4         Подчеркивание
  5         Мерцание
  6         Быстрое мерцание
  7         Негативное изображение
  8         Скрытое изображение (невидимое)

            Цвета изображения
  30        Черный
  31        Красный
  32        Зеленый
  33        Желтый
  34        Голубой
  35        Малиновый
  36        Бирюзовый
  37        Белый

            Цвета фона
  40        Черный
  41        Красный
  42        Зеленый
  43        Желтый
  44        Голубой
  45        Малиновый
  46        Бирюзовый
  47        Белый
*/


#define REMOTE_PIPE              2
#define DISCON_MODE              3
#define BAD_INPUT_ARGS           99
#define MAX_PIPE_NAME_LEN        80
#define MAX_SERV_NAME_LEN        8
#define DEFAULT_PIPE_NAME        "\\PIPE\\MYPIPE"
#define DEFAULT_MAKE_MODE        NP_ACCESS_DUPLEX
#define DEFAULT_PIPE_MODE        NP_WMESG | NP_RMESG | 0x01
#define DEFAULT_OPEN_FLAG        OPEN_ACTION_OPEN_IF_EXISTS
#define DEFAULT_OPEN_MODE        OPEN_FLAGS_WRITE_THROUGH | \
                                 OPEN_FLAGS_FAIL_ON_ERROR | \
                                 OPEN_FLAGS_RANDOM |        \
                                 OPEN_SHARE_DENYNONE |      \
                                 OPEN_ACCESS_READWRITE
#define DEFAULT_OUTB_SIZE        0x1000
#define DEFAULT_INPB_SIZE        0x1000
#define DEFAULT_TIME_OUTV        20000L
#define TOKEN_F3_DISCON          0x0000003DL
#define RETURN_CHAR              0x0D
#define LINE_FEED_CHAR           0x0A
#define FUNC_KEYS_CHAR           0x00
#define EXTD_KEYS_CHAR           0xE0
#define HAND_SHAKE_LEN           0x08
#define HAND_SHAKE_INP           "pIpEtEsT"
#define HAND_SHAKE_OUT           "PiPeTeSt"
#define HAND_SHAKE_ERROR         101
#define PROGRAM_ERROR            999

/*********************************************/
/* класс NPipe                               */
/*********************************************/

class NPipe
{
public:
   HPIPE   Hpipe;      /* the handle of the pipe */
   char name[256];     /* имя */
   ULONG   ulOpenMode; /*  A set of flags defining the mode in which to open the pipe. */
   ULONG   ulPipeMode; /*  A set of flags defining the mode of the pipe. */
   ULONG   ulOutBufSize ; /*  The number of bytes to allocate for the outbound (server to client) buffer. */
   ULONG   ulInpBufSize;  /*  The number of bytes to allocate for the inbound (client to server) buffer. */
   ULONG   ulTimeOut;     /*  The maximum time, in milliseconds, to wait for a named-pipe instance to become available. */
   int     mode;          /* SERVER_MODE - pабота как сеpвеp, CLIENT_MODE - как клиент */
   ULONG ulActionTaken;

   NPipe()
   {  Hpipe=NULL;
      name[0]=0;
      ulOpenMode   = DEFAULT_OPEN_MODE; /* DEFAULT_MAKE_MODE; for server */
      ulPipeMode   = DEFAULT_PIPE_MODE;
      ulOutBufSize = DEFAULT_OUTB_SIZE;
      ulInpBufSize = DEFAULT_INPB_SIZE;
      ulTimeOut    = DEFAULT_TIME_OUTV;
      mode = CLIENT_MODE;
   }
   NPipe(char *_name, int _mode)
   {
      strcpy(name, _name);
      Hpipe=NULL;
      ulOpenMode   = DEFAULT_MAKE_MODE;
      ulPipeMode   = DEFAULT_PIPE_MODE;
      ulOutBufSize = DEFAULT_OUTB_SIZE;
      ulInpBufSize = DEFAULT_INPB_SIZE;
      ulTimeOut    = DEFAULT_TIME_OUTV;
      mode = _mode;
      if(mode == CLIENT_MODE)
              ulOpenMode   = DEFAULT_OPEN_MODE;
      else    ulOpenMode   = DEFAULT_MAKE_MODE;
   }

   NPipe(char *_name, int OpenMode,int PipeMode,int OutBufSize,int InpBufsize,int Timeout, int _mode)
   {
      strcpy(name, _name);
      Hpipe=NULL;
      ulOpenMode   = OpenMode;
      ulPipeMode   = PipeMode;
      ulOutBufSize = OutBufSize;
      ulInpBufSize = InpBufsize;
      ulTimeOut    = Timeout;
      mode = _mode;
      if(mode == CLIENT_MODE)
              ulOpenMode   = DEFAULT_OPEN_MODE;
      else    ulOpenMode   = DEFAULT_MAKE_MODE;
   }

   ~NPipe()
   { if(Hpipe)
     {  DosClose(Hpipe);
     }
     Hpipe = NULL;
   }

   int Create(void)
   {  int rc;
      if(mode != SERVER_MODE)
               return -1;
      rc  = DosCreateNPipe(name,&Hpipe,
                           ulOpenMode,ulPipeMode,
                           ulOutBufSize,ulInpBufSize,
                           ulTimeOut);
      return rc;
   }
   int Connect(void)
   {  int rc;
      if(mode != SERVER_MODE)
               return -1;
      rc = DosConnectNPipe(Hpipe);
      return rc;
   }

   int Close(void)
   { if(Hpipe)
     {  DosClose(Hpipe);
     }
     Hpipe = NULL;
     return 0;
   }

   int Open(void)
   {  int rc;
      if(mode != CLIENT_MODE)
               return -1;

      rc = DosOpen(name,
                   &Hpipe,
                   &ulActionTaken,
                   0,
                   0,
                   DEFAULT_OPEN_FLAG, /* ulOpenFlag, */
                   ulOpenMode,
                   0);
      return rc;
   }
   int HandShake(void)
   {  int rc;
      if(mode == SERVER_MODE)
         rc = HandShakeServer();
      else
         rc = HandShakeClient();
      return rc;
   }


   int HandShakeClient(void)
   {  int rc,rc0;
      char str[256];
      ULONG ulBytesDone;

      rc = 1;

      rc0 = DosWrite(Hpipe,
                     HAND_SHAKE_INP,
                     strlen(HAND_SHAKE_INP)+1, /* с нулем на конце */
                     &ulBytesDone);
//printf("Клиент handshake -> %s\n",HAND_SHAKE_INP);
      if (!rc0)
      {  str[0] = 0;
         rc0 = DosRead(Hpipe,str,
                       (ULONG)strlen(HAND_SHAKE_OUT)+1,
                        &ulBytesDone);
//printf("Клиент handshake <- %s\n",str);
         if (strcmp(str,
                    HAND_SHAKE_OUT))
         {  rc = HAND_SHAKE_ERROR;
         } else rc =0;
      }
      return rc;
   }

   int HandShakeServer(void)
   {  int rc,rc0;
      char str[256];
      ULONG ulBytesDone;

      rc = 1;
      str[0] = 0;
      rc0 = DosRead(Hpipe,str,
                       (ULONG)strlen(HAND_SHAKE_INP)+1,
                        &ulBytesDone);
//printf("Сеpвеp handshake <- %s\n",str);

      if (strcmp(str, HAND_SHAKE_INP))
      {  rc = HAND_SHAKE_ERROR;
      } else {
         rc0 = DosWrite(Hpipe,
                     HAND_SHAKE_OUT,
                     strlen(HAND_SHAKE_OUT)+1, /* с нулем на конце */
                     &ulBytesDone);
//printf("Сеpвеp handshake -> %s\n",HAND_SHAKE_OUT);
         if(!rc0)
               rc =0;

      }
      return  rc;
   }
/* послать команду ncmd с данными data */
   int SendCmdToServer(int ncmd, int data)
   {  char str[32];
      int rc, *pdata;
      ULONG ulBytesDone;

      pdata = (int *)&str[0];
      *pdata = ncmd;
      pdata[1] = data;
      rc = DosWrite(Hpipe,(void *)pdata,sizeof(int)*2, &ulBytesDone);
      if(ulBytesDone != sizeof(int)*2  && rc == 0)
         rc = -1;
      return rc;
   }

   int SendDataToServer(void *data, int len)
   {   int rc;
       ULONG ulBytesDone;
       rc = DosWrite(Hpipe,data,len, &ulBytesDone);
      if(ulBytesDone != len && rc == 0)
         rc = -1;
       return rc;
   }

   int RecvDataFromClient(void *data, int *len, int maxlen)
   { int rc;

     rc =   DosRead(Hpipe, data, maxlen,(PULONG) len);
     return rc;
   }

   int RecvCmdFromClient(int *ncmd, int *data)
   {  int rc, *pdata;
      ULONG ulBytesDone;
      char str[32];

     rc =  DosRead(Hpipe, (void *)str, sizeof(int) * 2,&ulBytesDone);
     pdata = (int *)&str[0];
     *ncmd =  *pdata;
     *data =  pdata[1];
      if(ulBytesDone != sizeof(int) * 2)
      {   if(rc == 0)
          {   if(ulBytesDone == 0) *ncmd = 0; //pipe is closed
              else rc = -1;
          }
      }
     return rc;
   }

   int QueryState(void)
   {  int rc;
      ULONG state;
      rc = DosQueryNPHState(Hpipe,&state);
      return rc;
   }

};

/*********************************************/

/* SQDR_queue.cpp */

 #define INCL_DOS
 #define INCL_DOSERRORS   /* DOS error values */
 #define INCL_DOSQUEUES   /* Queue values */
     #include <os2.h>
     #include <stdio.h>

/*+---------------------------------+*/
/*| Internal function prototypes.   |*/
/*+---------------------------------+*/
int InitQueue(void);
int ReadQueue(int threadNum);
int AddToQueue(int threadNum);

/*+--------------------------------------------------------+*/
/*| Static global variables and local constants.           |*/
/*+--------------------------------------------------------+*/
char nameSQDRQueue[]= "\\QUEUES\\SQDR_QUEUE";
char nameSQDRSem[]  = "\\SEM32\\SQDR_EVENTQUE";

HQUEUE  hqWait;
HEV     hsmSQDR;

/* Create Queue */
int InitQueue(void)
{  int rc;
   rc = DosCreateQueue( &hqWait, QUE_FIFO | QUE_CONVERT_ADDRESS, (PSZ)nameSQDRQueue);
   rc  = DosCreateEventSem(nameSQDRSem,&hsmSQDR,0L, TRUE);

   return rc;
}

int AddToQueue(int threadNum)
{
     ULONG    ulDataLength;    /* Length of element being added        */
     PVOID    pDataBuffer;     /* Element being added                  */
     ULONG    ulElemPriority;  /* Priority of element being added      */
     APIRET   ulrc;            /* Return code                          */

     ulElemPriority = 0;       /* For priority-based queues: add the   */
                               /* new queue element at the logical end */
                               /* of the queue                         */
     pDataBuffer = NULL;
     ulDataLength = 0;
     ulrc = DosWriteQueue(hqWait,
                          threadNum,
                          ulDataLength,
                          pDataBuffer,
                          ulElemPriority);

     if (ulrc != 0) {
         printf("DosWriteQueue error: return code = %ld",
                ulrc);
         return 1;
     }
     return 0;
}

int ReadQueue(int threadNum)
{
    REQUESTDATA Request      = {0};        /* Reques */
    PSZ         DataBuffer   = "";         /* Data buffer for queue data     */
    ULONG       ulDataLen    = 0;          /* Length of data returned        */
    BYTE        ElemPrty     = 0;          /* Priority of element (returned) */
    int rc, retrc=1;
    rc = DosPeekQueue (hqWait,          /* Queue to read from          */
                       &Request,              /* Request data from write     */
                       &ulDataLen,            /* Length of data returned     */
                       (PVOID *) &DataBuffer,   /* The data                    */
                       0L,                    /* Remove first element        */
                       DCWW_NOWAIT,             /* Wait for available data     */
                       &ElemPrty,             /* Priority of data (returned) */
                       hsmSQDR);             /* Semaphore to use when not waiting */

    if(rc == ERROR_QUE_EMPTY)
             retrc = 0;
    else
       if(Request.ulData ==  threadNum)
    {    retrc = 0;
         rc = DosReadQueue (hqWait,          /* Queue to read from          */
                       &Request,              /* Request data from write     */
                       &ulDataLen,            /* Length of data returned     */
                       (PVOID *) &DataBuffer,   /* The data                    */
                       0L,                    /* Remove first element        */
                       DCWW_NOWAIT,             /* Wait for available data     */
                       &ElemPrty,             /* Priority of data (returned) */
                       hsmSQDR);             /* Semaphore to use when not waiting */

    }  else {
         retrc = 1;
    }

    return retrc;
}





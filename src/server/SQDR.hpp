/* SQDR.h */

char SQDRmutexName[] = "\\SEM32\\SQDRserver"; /* Semaphore name */

#define STACK_SIZE     32000UL + MAX_BUF*13   /* stack size for thread */
#define LOCKED    1
#define UNLOCKED  0
#define MAX_NUM_THREADS 32

//��८��筮��� �஢�ન ���䨣� �� ��������� �� �� 祬, � �ᥪ㭤��
#define CHECK_CONFIG_TIME  100

/*********************************************/
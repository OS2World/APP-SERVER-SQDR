/* sg.h */

#define MAX_BUF 4096

#define DEFAULT_LOGFILE "sqdr.log"
#define WARNING_LOGFILE "sqdr.log"
#define ERROR_LOGFILE   "sqdr.error"

#define DEFAULT_CONFIGFILE "sqdr.conf"
#define DEFAULT_LOGDIR "SQDR"
/* "logs" */

typedef unsigned short ino_t;
typedef short dev_t;


struct LogFileStat {
  char *name;
  FILE *fd;
  ino_t st_ino;
  dev_t st_dev;
  struct LogFileStat *next;
};

struct LogFile {
  char *parent_name;
  int parent_type;
  int anonymous;
  struct LogFileStat *stat;
};


struct SquidQueue {
  struct SquidInfo *squidInfo;
  struct SquidQueue *next;
};

struct SquidInfo {
  char protocol[MAX_BUF];
  char domain[MAX_BUF];
  int  dot;  /* true if domain is in dot notation */
  char url[MAX_BUF];
  char orig[MAX_BUF];
  char surl[MAX_BUF];
  char *strippedurl;
  int  port;
  char src[MAX_BUF];
  char srcDomain[MAX_BUF];
  char ident[MAX_BUF];
  char method[MAX_BUF];
};


struct Source {
  char *name;
  int active;
//  struct Ip *ip;
//  struct Ip *lastip;
//  struct sgDb *domainDb;
//  struct sgDb *userDb;
//  struct Time *time;
  int within;
//  struct LogFile *logfile;
  struct Source *next;
};

struct Acl {
  char *name;
  int active;
  struct Source *source;
//  struct AclDest *pass;
  int rewriteDefault;
  struct sgRewrite *rewrite;
  char *redirect;
  struct Time *time;
  int within;
  struct LogFile *logfile;
  struct Acl *next;
};

struct AclDest {
  char *name;
//  struct Destination *dest;
  int    access;
  int    type;
  struct AclDest *next;
};


void   sgSetGlobalErrorLogFile(void);
void   sgReloadConfig(void);
int sgCheckForReloadConfig(char *fname,int checktime);
void   sgLog(struct LogFileStat *log, char *format, ...);
void   sgLogError(char *, ...);
void   sgLogFatalError(char *format, ...);
struct LogFileStat *sgLogFileStat(char *file);

int    parseLine(char *, struct SquidInfo *);

struct Source *sgFindSource(char *, char *, char *);
char   *sgAclAccess(struct Source *, struct Acl *, struct SquidInfo *);
struct Acl *sgAclCheckSource(struct Source *);
char   *niso(time_t tt);
void   sgEmergency(void);
void   * sgMalloc(size_t);
void   *sgCalloc(size_t, size_t);
void   *sgRealloc(void *, size_t);
void   sgFree(void *);
void sgHandlerSigHUP(int signal);



int gettimeofday (struct timeval *, struct timezone *);


extern char *progname;
extern struct LogFileStat *LogFileStat;
extern struct LogFileStat *lastLogFileStat;

extern int sig_hup;
extern int sig_alrm;


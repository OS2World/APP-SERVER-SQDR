/* sgDiv.cpp */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#include "sg.h"


/*
  parsers the squidline:
  URL ip-address/fqdn ident method
*/

int parseLine(char *line, struct SquidInfo *s)
{
  char *p, *d = NULL, *a = NULL, *e = NULL, *o, *field;
  char *up, *upp;
  int i = 0, hex;
  char c,h1,h2;
  field = strtok(line,"\t ");
  if(field == NULL)
    return 0;
  strcpy(s->orig,field);
  for(p=field; *p != '\0'; p++) /* convert url to lowercase chars */
    *p = tolower(*p);
  s->url[0] = s->protocol[0] = s->domain[0] = s->src[0] = s->ident[0] =
    s->method[0] = s->srcDomain[0] = s->surl[0] =  '\0';
  s->dot = 0;
  s->port = 0;
  p = strstr(field,"://");
  if(p == NULL) { /* no protocl, defaults to http */
    strcpy(s->protocol,"unknown");
    p = field;
  } else {
    strncpy(s->protocol,field,p - field);
    *(s->protocol + ( p - field)) = '\0';
    p+=3;
  }
    /*do some url decoding */
  up=field;
  upp= s->url;
  while(up[i] != '\0'){
    if(up[i] == '%'){
      if(isxdigit(up[i+1]) && isxdigit(up[i+2])){
        h1 = up[i+1] >= 'a' ? up[i+1] - ('a' - 'A') : up[i+1];
        h2 = up[i+2] >= 'a' ? up[i+2] - ('a' - 'A') : up[i+2];
        hex = h1 >= 'A' ? h1 - 'A' - 10 : h1 - '0';
        hex *= 16;
        hex += h2 >= 'A' ? h2 - 'A' - 10 : h2 - '0';
       /* don't convert whitespace, newline and carriage return */
       if(hex == 0x20 || hex == 0x09 || hex == 0x0a || hex == 0x0d){
         *upp++ = up[i++];
         *upp++ = up[i++];
         *upp++ = up[i];
       } else {
         *upp++ = hex;
         i+=2;
       }
      } else { /* an errorous hex code, we ignore it */
        *upp++ = up[i++];
        *upp++ = up[i++];
        *upp++ = up[i];
      }
    } else {
      *upp++ = up[i];
    }
    i++;
  }
  *upp++=up[i];
  *upp='\0';
  i=0;
  d = strchr(p,'/'); /* find domain end */
  e = d;
  a = strchr(p,'@'); /* find auth  */
  if(a != NULL && ( a < d || d == NULL))
    p = a + 1;
  a = strchr(p,':'); /* find port */;
  if(a != NULL && (a < d || d == NULL)){
    o = a + strspn(a+1,"0123456789") + 1;
    c = *o;
    *o = '\0';
    s->port = atoi(a+1);
    *o = c;
    e = a;
  }
  o=p;
  if (p[0] == 'w' || p[0] == 'f' ) {
    if ((p[0] == 'w' && p[1] == 'w' && p[2] == 'w') ||
       (p[0] == 'w' && p[1] == 'e' && p[2] == 'b') ||
       (p[0] == 'f' && p[1] == 't' && p[2] == 'p')) {
      p+=3;
      while (p[0] >= '0' && p[0] <= '9')
       p++;
      if (p[0] != '.')
       p=o; /* not a hostname */
      else
       p++;
    }
  }
  if(e == NULL){
    strcpy(s->domain,o);
    strcpy(s->surl,p);
  }
  else {
    strncpy(s->domain,o,e - o);
    strcpy(s->surl,p);
    *(s->domain + (e - o)) = '\0';
    *(s->surl + (e - p)) = '\0';
  }
  //strcpy(s->surl,s->domain);
  if(strspn(s->domain,".0123456789") == strlen(s->domain))
    s->dot = 1;
  if(d != NULL)
    strcat(s->surl,d);
  s->strippedurl = s->surl;
  while((p = strtok(NULL," \t\n")) != NULL){
    switch(i){
    case 0: /* src */
      o = strchr(p,'/');
      if(o != NULL){
       strncpy(s->src,p,o-p);
       strcpy(s->srcDomain,o+1);
       s->src[o-p]='\0';
       if(*s->srcDomain == '-')
         s->srcDomain[0] = '\0';
      } else
       strcpy(s->src,p);
      break;
    case 1: /* ident */
      if(strcmp(p,"-")){
       strcpy(s->ident,p);
       for(p=s->ident; *p != '\0'; p++) /* convert ident to lowercase chars */
         *p = tolower(*p);
      } else
       s->ident[0] = '\0';
      break;
    case 2: /* method */
      strcpy(s->method,p);
      break;
    }
    i++;
  }
  if(s->domain[0] == '\0')
    return 0;
  if(s->method[0] == '\0')
    return 0;
  return 1;
}

void *sgMalloc(size_t elsize)
{
  void *p;
  if((p=(void *) malloc(elsize)) == NULL)
  {
    sgLogFatalError("%s: %s",progname,strerror(ENOMEM));
    exit(1);
  }
  return (void *) p;
}

void *sgCalloc(size_t nelem, size_t elsize)
{
  void *p;
  if((p=(void *) calloc(nelem,elsize)) == NULL)
  {
    sgLogFatalError("%s: %s",progname,strerror(ENOMEM));
    exit(1);
  }
  return (void *) p;
}


void *sgRealloc(void *ptr, size_t elsize)
{
  void *p;
  if((p=(void *) realloc(ptr,elsize)) == NULL)
  {
    sgLogFatalError("%s: %s",progname,strerror(ENOMEM));
    exit(1);
  }
  return (void *) p;
}

void sgFree(void *ptr)
{
  free(ptr);
}


# makefile
# Created by IBM WorkFrame/2 MakeMake at 1:57:21 on 2 April 2001
#
# The actions included in this make file are:
#  Compile::C++ Compiler
#  Link::Linker

.SUFFIXES:

.SUFFIXES: \
    .c .cpp .obj 

.cpp.obj:
    @echo " Compile::C++ Compiler "
    icc.exe /Ss /Ti /O /G5 /Tm /C %s

{G:\EVGEN\INET\SQDR}.cpp.obj:
    @echo " Compile::C++ Compiler "
    icc.exe /Ss /Ti /O /G5 /Tm /C %s

.c.obj:
    @echo " Compile::C++ Compiler "
    icc.exe /Ss /Ti /O /G5 /Tm /C %s

{G:\EVGEN\INET\SQDR}.c.obj:
    @echo " Compile::C++ Compiler "
    icc.exe /Ss /Ti /O /G5 /Tm /C %s

all: \
    .\target.exe

.\target.exe: \
    .\sgDiv.obj \
    .\sgLog.obj \
    .\getopt.obj \
    .\main.obj \
    {$(LIB)}TCP32DLL.LIB \
    {$(LIB)}SO32DLL.LIB
    @echo " Link::Linker "
    icc.exe @<<
     /B" /exepack:2 /packd /optfunc"
     /Fetarget.exe 
     TCP32DLL.LIB 
     SO32DLL.LIB 
     .\sgDiv.obj
     .\sgLog.obj
     .\getopt.obj
     .\main.obj
<<

.\sgDiv.obj: \
    G:\EVGEN\INET\SQDR\sgDiv.cpp \
    {G:\EVGEN\INET\SQDR;$(INCLUDE);}sg.h

.\main.obj: \
    G:\EVGEN\INET\SQDR\main.cpp \
    {G:\EVGEN\INET\SQDR;$(INCLUDE);}sg.h \
    {G:\EVGEN\INET\SQDR;$(INCLUDE);}getopt.h \
    {G:\EVGEN\INET\SQDR;$(INCLUDE);}SQDRclient.hpp

.\getopt.obj: \
    G:\EVGEN\INET\SQDR\getopt.c \
    {G:\EVGEN\INET\SQDR;$(INCLUDE);}getopt.h

.\sgLog.obj: \
    G:\EVGEN\INET\SQDR\sgLog.cpp \
    {G:\EVGEN\INET\SQDR;$(INCLUDE);}sg.h

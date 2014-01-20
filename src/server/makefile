# makefile
# Created by IBM WorkFrame/2 MakeMake at 14:51:58 on 4 Jan 2002
#
# The actions included in this make file are:
#  Compile::C++ Compiler
#  Link::Linker

.SUFFIXES:

.SUFFIXES: \
    .cpp .obj 

.cpp.obj:
    @echo " Compile::C++ Compiler "
    icc.exe /Ss /Ti /O /Gm /G5 /Tm /Gi /Oi /C %s

{G:\EVGEN\INET\SQDR\Server}.cpp.obj:
    @echo " Compile::C++ Compiler "
    icc.exe /Ss /Ti /O /Gm /G5 /Tm /Gi /Oi /C %s

all: \
    .\sqdrserver.exe

.\sqdrserver.exe: \
    .\SQDRserver.obj \
    .\SQDR_queue.obj \
    .\sgDiv.obj \
    .\sgLog.obj \
    {$(LIB)}TCP32DLL.LIB \
    {$(LIB)}SO32DLL.LIB
    @echo " Link::Linker "
    icc.exe @<<
     /B" /exepack:2 /packd /optfunc"
     /Fesqdrserver.exe 
     TCP32DLL.LIB 
     SO32DLL.LIB 
     .\SQDRserver.obj
     .\SQDR_queue.obj
     .\sgDiv.obj
     .\sgLog.obj
<<

.\SQDRserver.obj: \
    G:\EVGEN\INET\SQDR\Server\SQDRserver.cpp \
    {G:\EVGEN\INET\SQDR\Server;$(INCLUDE);}sg.h \
    {G:\EVGEN\INET\SQDR\Server;$(INCLUDE);}SQDR.hpp \
    {G:\EVGEN\INET\SQDR\Server;$(INCLUDE);}SQDRclient.hpp

.\sgLog.obj: \
    G:\EVGEN\INET\SQDR\Server\sgLog.cpp \
    {G:\EVGEN\INET\SQDR\Server;$(INCLUDE);}sg.h

.\sgDiv.obj: \
    G:\EVGEN\INET\SQDR\Server\sgDiv.cpp \
    {G:\EVGEN\INET\SQDR\Server;$(INCLUDE);}sg.h

.\SQDR_queue.obj: \
    G:\EVGEN\INET\SQDR\Server\SQDR_queue.cpp

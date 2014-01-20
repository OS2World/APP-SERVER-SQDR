# makefile
# Created by IBM WorkFrame/2 MakeMake at 1:05:56 on 14 April 2001
#
# The actions included in this make file are:
#  Compile::C++ Compiler
#  Link::Linker

.SUFFIXES:

.SUFFIXES: \
    .c .cpp .obj 

.cpp.obj:
    @echo " Compile::C++ Compiler "
    icc.exe /Ss /Ti /G5 /Tm /C %s

{G:\EVGEN\INET\SQDR\SQ_test}.cpp.obj:
    @echo " Compile::C++ Compiler "
    icc.exe /Ss /Ti /G5 /Tm /C %s

.c.obj:
    @echo " Compile::C++ Compiler "
    icc.exe /Ss /Ti /G5 /Tm /C %s

{G:\EVGEN\INET\SQDR\SQ_test}.c.obj:
    @echo " Compile::C++ Compiler "
    icc.exe /Ss /Ti /G5 /Tm /C %s

all: \
    .\Emul.exe

.\Emul.exe: \
    .\sgDiv.obj \
    .\sgLog.obj \
    .\getopt.obj \
    .\mainEmul.obj \
    {$(LIB)}TCP32DLL.LIB \
    {$(LIB)}SO32DLL.LIB
    @echo " Link::Linker "
    icc.exe @<<
     /B" /de /exepack:2 /packd /optfunc"
     /FeEmul.exe 
     TCP32DLL.LIB 
     SO32DLL.LIB 
     .\sgDiv.obj
     .\sgLog.obj
     .\getopt.obj
     .\mainEmul.obj
<<

.\sgDiv.obj: \
    G:\EVGEN\INET\SQDR\SQ_test\sgDiv.cpp \
    {G:\EVGEN\INET\SQDR\SQ_test;$(INCLUDE);}sg.h

.\mainEmul.obj: \
    G:\EVGEN\INET\SQDR\SQ_test\mainEmul.cpp \
    {G:\EVGEN\INET\SQDR\SQ_test;$(INCLUDE);}sg.h \
    {G:\EVGEN\INET\SQDR\SQ_test;$(INCLUDE);}getopt.h \
    {G:\EVGEN\INET\SQDR\SQ_test;$(INCLUDE);}SQDRclient.hpp

.\getopt.obj: \
    G:\EVGEN\INET\SQDR\SQ_test\getopt.c \
    {G:\EVGEN\INET\SQDR\SQ_test;$(INCLUDE);}getopt.h

.\sgLog.obj: \
    G:\EVGEN\INET\SQDR\SQ_test\sgLog.cpp \
    {G:\EVGEN\INET\SQDR\SQ_test;$(INCLUDE);}sg.h

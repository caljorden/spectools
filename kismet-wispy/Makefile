# Kismet wireless sniffer

KIS_INC_DIR		= /home/dragorn/src/kismet-newcore/

HOME	= .
CXX	= g++
CC	= gcc
LD	= g++
LDFLAGS	= -shared -rdynamic
LIBS	= -lstdc++ -lusb -lpthread
CFLAGS	= -fPIC -g
CXXFLAGS = -fPIC -pthread -g
CPPFLAGS = -I/usr/include -I../ -I$(KIS_INC_DIR)

PLUGOBJS = wispy_container.o wispy_hw_gen1.o kismet-wispy.o
CPLUGOBJS = kismet-wispy-client.o

all:	kismet-wispy.so kismet-wispy-client.so

kismet-wispy.so:	$(PLUGOBJS)
	$(LD) $(LDFLAGS) $(PLUGOBJS) -o kismet-wispy.so $(LIBS)

kismet-wispy-client.so:	$(CPLUGOBJS)
	$(LD) $(LDFLAGS) $(CPLUGOBJS) -o kismet-wispy-client.so $(LIBS)

clean:
	@-rm *.o *.so

.c.o:
	$(CC) $(CFLAGS) -c $*.c -o $@ 

.cc.o:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $*.cc -o $@ 

.SUFFIXES: .c .cc .o

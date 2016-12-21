CC = cc
CFLAGS = -Wall -O2
LIBS =
LIBDIR = /usr/local/lib
INCLUDEDIR = /usr/local/include
INSTALL = install
HEADERS = ../common.h pccc.h private.h
LIBNAME = libpccc
MAJOR_VER = 1
MINOR_VER = 1
OBJECTS = addr.o cmd_init.o cmd_init_06.o cmd_init_0f.o data.o msg.o pccc.o reply.o sts.o

all : libpccc

docs : *.c *.h
	doxygen Doxyfile
	mv docs/html/* docs
	rm -r docs/html

libpccc : $(OBJECTS)
	$(CC) $(LIBS) -shared -Wl,-soname,$(LIBNAME).so.$(MAJOR_VER) -o \
	$(LIBNAME).so.$(MAJOR_VER).$(MINOR_VER) $(OBJECTS) \
	../buf.o ../byteorder.o

main.o : main.c pccc.h
	$(CC) $(CFLAGS) -c main.c

addr.o : addr.c $(HEADERS)
	$(CC) $(CFLAGS) -c addr.c

cmd_init.o : cmd_init.c $(HEADERS)
	$(CC) $(CFLAGS) -c cmd_init.c

cmd_init_06.o : cmd_init_06.c $(HEADERS)
	$(CC) $(CFLAGS) -c cmd_init_06.c

cmd_init_0f.o : cmd_init_0f.c $(HEADERS)
	$(CC) $(CFLAGS) -c cmd_init_0f.c

data.o : data.c $(HEADERS)
	$(CC) $(CFLAGS) -c data.c

msg.o : msg.c $(HEADERS)
	$(CC) $(CFLAGS) -c msg.c

pccc.o : pccc.c $(HEADERS)
	$(CC) $(CFLAGS) -c pccc.c

reply.o : reply.c $(HEADERS)
	$(CC) $(CFLAGS) -c reply.c

sts.o : sts.c $(HEADERS)
	$(CC) $(CFLAGS) -c sts.c

install :
	$(INSTALL) --group=root --owner=root \
	$(LIBNAME).so.$(MAJOR_VER).$(MINOR_VER) $(LIBDIR)
	ldconfig
	ln -sf $(LIBDIR)/$(LIBNAME).so.$(MAJOR_VER) $(LIBDIR)/$(LIBNAME).so
	$(INSTALL) --group=root --owner=root --mode=644 pccc.h $(INCLUDEDIR)

clean :
	rm -fr $(LIBNAME).so.* *.o *~ docs

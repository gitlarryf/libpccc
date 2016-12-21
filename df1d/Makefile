CC = cc
CFLAGS = -Wall -O2 `xml2-config --cflags`
INSTALL = install
BINDIR = /usr/local/bin
LIBS = `xml2-config --libs`
OBJECTS = cfg.o client.o conn.o log.o main.o rx.o timer.o tty.o tx.o

all : df1d

df1d : $(OBJECTS)
	$(CC) $(LIBS) -o df1d $(OBJECTS) ../buf.o ../byteorder.o

cfg.o : cfg.c df1.h
	$(CC) $(CFLAGS) -c cfg.c

client.o : client.c df1.h
	$(CC) $(CFLAGS) -c client.c

conn.o : conn.c df1.h
	$(CC) $(CFLAGS) -c conn.c

log.o : log.c df1.h
	$(CC) $(CFLAGS) -c log.c

main.o : main.c df1.h
	$(CC) $(CFLAGS) -c main.c

rx.o : rx.c df1.h
	$(CC) $(CFLAGS) -c rx.c

timer.o : timer.c df1.h
	$(CC) $(CFLAGS) -c timer.c

tty.o : tty.c df1.h
	$(CC) $(CFLAGS) -c tty.c

tx.o : tx.c df1.h
	$(CC) $(CFLAGS) -c tx.c

install :
	$(INSTALL) --group=root --owner=root df1d $(BINDIR)

clean :
	rm -f df1d *.o *~

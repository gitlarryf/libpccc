CC = cc
CFLAGS = -Wall -O2

all : common
	cd lib && make
	cd df1d && make

common : buf.o byteorder.o

buf.o : buf.c common.h
	$(CC) $(CFLAGS) -c buf.c

byteorder.o : byteorder.c common.h
	$(CC) $(CFLAGS) -c byteorder.c

install :
	cd lib && make install
	cd df1d && make install

clean :
	rm -f *.o *~
	cd lib && make clean
	cd df1d && make clean
DEBUG	= -Wall -g
CFLAGS	= -I../../include $(DEBUG)
LDFLAGS	= $(DEBUG)
LIBS	= libcardreader.so
OBJS	= buffer.o socket.o

all:: $(LIBS)

clean distclean::
	rm -rf *.so *.o

libcardreader.so: $(OBJS)
	$(CC) -shared -o $@ $(OBJS)

%.o: %.c
	$(CC) -fPIC $(CFLAGS) -c $< -o $@

DEBUG	= -Wall -g
CFLAGS	= -I../../include $(DEBUG)
LDFLAGS	= $(DEBUG)
LIBS	= libcardreader.so
OBJS	= buffer.o socket.o tlv.o error.o conf.o \
	  client.o

all:: $(LIBS)

clean distclean::
	rm -rf *.so *.o

libcardreader.so: $(OBJS)
	$(CC) -shared -o $@ $(OBJS)
	ln -sf $$PWD/$@ ../../lib/$@

%.o: %.c
	$(CC) -fPIC $(CFLAGS) -c $< -o $@

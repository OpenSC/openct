DEBUG	= -Wall -g
CFLAGS	= -I../../include $(DEBUG)
LDFLAGS	= $(DEBUG)
LIBS	= libcardreader.so
OBJS	= buffer.o socket.o error.o conf.o

all:: $(LIBS)

clean distclean::
	rm -rf *.so *.o

libcardreader.so: $(OBJS)
	$(CC) -shared -o $@ $(OBJS)
	ln -sf $$PWD/$@ ../../lib/$@

%.o: %.c
	$(CC) -fPIC $(CFLAGS) -c $< -o $@

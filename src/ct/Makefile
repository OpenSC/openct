DEBUG	= -Wall -g
CFLAGS	= -I../../include $(DEBUG)
LDFLAGS	= $(DEBUG)
LIBS	= libopenct.so
OBJS	= buffer.o socket.o tlv.o error.o \
	  client.o status.o mainloop.o
LIBDIR	= $(DESTDIR)/usr/lib

all:: $(LIBS)

clean distclean::
	rm -rf *.so *.o

install:
	mkdir -p $(LIBDIR)
	install -m 555 libopenct.so $(LIBDIR)

libopenct.so: $(OBJS)
	$(CC) -shared -o $@ $(OBJS)
	ln -sf $$PWD/$@ ../../lib/$@

%.o: %.c
	$(CC) -fPIC $(CFLAGS) -c $< -o $@

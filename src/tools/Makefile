DEBUG	= -Wall -g
CFLAGS	= -I../../include $(DEBUG)
LDFLAGS	= $(DEBUG)
APPS	= openct-tool
OBJS	= 
BINDIR	= $(DESTDIR)/usr/bin
SBINDIR	= $(DESTDIR)/usr/sbin

all:: $(APPS)

clean distclean::
	rm -rf *.so *.a *.o $(APPS)

install:
	mkdir -p $(BINDIR) $(SBINDIR)
	install -m 555 -s openct-tool $(BINDIR)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

openct-tool: openct-tool.o $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $@.o $(OBJS) \
		-L../../lib -lopenct

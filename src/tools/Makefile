DEBUG	= -Wall -g
CFLAGS	= -I../../include $(DEBUG)
LDFLAGS	= $(DEBUG)
APPS	= openct-tool
OBJS	= 

all:: $(APPS)

clean distclean::
	rm -rf *.so *.a *.o $(APPS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

openct-tool: openct-tool.o $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $@.o $(OBJS) \
		-L../../lib -lopenct

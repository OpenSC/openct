CFLAGS	= $(DEBUG) -Wall -fPIC -I../../include
ALL	= towitoko.so

all:	$(ALL)

clean distclean:
	rm -f *.o *.so

%.so: %.c
	$(CC) -shared -o $@ $(CFLAGS) $<



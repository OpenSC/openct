CFLAGS	= $(DEBUG) -Wall -fPIC -I../../include
DEBUG	= -g
ALL	= towitoko.so etoken.so kaan.so

all:	$(ALL)

clean distclean:
	rm -f *.o *.so

%.so: %.c
	$(CC) $(DEBUG) -shared -o $@ $(CFLAGS) $<



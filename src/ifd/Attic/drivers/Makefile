CFLAGS	= $(DEBUG) -Wall -fPIC -I../../include
DEBUG	= -g
ALL	= towitoko.so etoken.so kaan.so
CTAPI	= $(addprefix ctapi-,$(ALL))
LIBS	= -L.. -lifd

all:	$(ALL) $(CTAPI)

clean distclean:
	rm -f *.o *.so

ctapi-%.so: ctapi.c
	driver=`expr $@ : 'ctapi-\(.*\)\.so'`; \
	$(CC) $(DEBUG) -shared -DDRIVER_NAME='"'$$driver'"' -o $@ $(CFLAGS) $< $(LIBS)

%.so: %.c
	$(CC) $(DEBUG) -shared -o $@ $(CFLAGS) $< $(LIBS)



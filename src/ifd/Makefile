DEBUG	= -Wall -g
CFLAGS	= -I../../include $(DEBUG)
LDFLAGS	= $(DEBUG)
LIBS	= libifd.a libifd.so
APPS	= print-atr ifdhandler
DRIVERS	= etoken.c kaan.c towitoko.c
LIBOBJS	= init.o manager.o device.o reader.o driver.o \
          modules.o conf.o \
	  protocol.o proto-t0.o proto-t1.o proto-trans.o \
	  serial.o usb.o \
	  $(DRIVERS:.c=.o) \
	  apdu.o checksum.o utils.o \
	  $(SYSDEP)
IFDOBJS	= ifdhandler.o process.o locks.o
SYSDEP	= linux.o
LINK	= -L../../lib -lopenct -ldl -lusb
LIBDIR	= $(DESTDIR)/usr/lib

all:: $(LIBS) $(APPS)

clean distclean::
	rm -rf .objs .sobjs *.so *.a *.o $(APPS)

install:
	mkdir -p $(LIBDIR)
	install -m 444 libifd.a $(LIBDIR)
	install -m 555 libifd.so $(LIBDIR)

libifd.a: $(addprefix .objs/,$(LIBOBJS))
	ar cr $@ $(addprefix .objs/,$(LIBOBJS))
	ranlib $@

libifd.so: $(addprefix .sobjs/,$(LIBOBJS))
	$(CC) -shared -o $@ $(addprefix .sobjs/,$(LIBOBJS)) $(LINK)
	ln -sf $$PWD/$@ ../../lib/$@

.objs/%.o: %.c
	@mkdir -p .objs
	$(CC) $(CFLAGS) -c $< -o $@

.sobjs/%.o: %.c
	@mkdir -p .sobjs
	$(CC) -fPIC $(CFLAGS) -c $< -o $@

print-atr: print-atr.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $@.o -L. -lifd $(LINK)

ifdhandler: $(IFDOBJS) $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $(IFDOBJS) -L. -lifd $(LINK)

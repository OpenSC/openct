DEBUG	= -Wall -g
CFLAGS	= -I../../include $(DEBUG)
LDFLAGS	= $(DEBUG)
LIBS	= libifd.a libifd.so
APPS	= print-atr
DRIVERS	= etoken.c kaan.c towitoko.c
OBJS	= init.o manager.o device.o reader.o driver.o hotplug.o \
          modules.o conf.o \
	  protocol.o proto-t0.o proto-t1.o proto-trans.o \
	  serial.o usb.o \
	  $(DRIVERS:.c=.o) \
	  apdu.o checksum.o utils.o \
	  $(SYSDEP)
SYSDEP	= linux.o
LINK	= -L../../lib -lopenct -ldl -lusb

all:: $(LIBS) $(APPS)

clean distclean::
	rm -rf .objs .sobjs *.so *.a *.o $(APPS)

libifd.a: $(addprefix .objs/,$(OBJS))
	ar cr $@ $(addprefix .objs/,$(OBJS))
	ranlib $@

libifd.so: $(addprefix .sobjs/,$(OBJS))
	$(CC) -shared -o $@ $(addprefix .sobjs/,$(OBJS)) $(LINK)
	ln -sf $$PWD/$@ ../../lib/$@

.objs/%.o: %.c
	@mkdir -p .objs
	$(CC) $(CFLAGS) -c $< -o $@

.sobjs/%.o: %.c
	@mkdir -p .sobjs
	$(CC) -fPIC $(CFLAGS) -c $< -o $@

print-atr: print-atr.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $@.o -L. -lifd $(LINK)

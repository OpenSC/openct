DEBUG	= -Wall -g
CFLAGS	= -I../include $(DEBUG)
LDFLAGS	= -Wl,-export-dynamic
LIBS	= libifd.a
APPS	= print-atr
OBJS	= manager.o device.o reader.o driver.o \
          modules.o \
	  protocol.o proto-t0.o proto-t1.o \
	  serial.o usb.o \
	  apdu.o checksum.o buffer.o error.o utils.o \
	  config.o $(SYSDEP)
SYSDEP	= linux.o
SUBDIRS	= drivers

all:: $(LIBS) $(APPS)

clean distclean::
	rm -rf .objs *.a *.o $(APPS)

all clean distclean::
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@; \
	done

libifd.a: $(addprefix .objs/,$(OBJS))
	ar cr $@ $(addprefix .objs/,$(OBJS))
	ranlib $@

.objs/%.o: %.c
	@mkdir -p .objs
	$(CC) $(CFLAGS) -c $< -o $@

print-atr: print-atr.o $(LIBS)
	$(CC) $(LDFLAGS) -o $@ $@.o -L. -lifd -ldl

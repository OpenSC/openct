DEBUG	= -Wall -g
CFLAGS	= -I../../include -I/usr/include/pcsc $(DEBUG)
LIBS	= ifdhandler.so
OBJS	= pcsc.o

all:: $(LIBS) $(APPS)

clean distclean::
	rm -rf .sobjs $(LIBS)

ifdhandler.so: $(addprefix .sobjs/,$(OBJS))
	ld -shared -o $@ $(addprefix .sobjs/,$(OBJS)) -L../libifd -lifd

.sobjs/%.o: %.c
	@mkdir -p .sobjs
	$(CC) -fPIC $(CFLAGS) -c $< -o $@

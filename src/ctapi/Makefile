DEBUG	= -Wall -g
CFLAGS	= -I../../include $(DEBUG)
LIBS	= ifd-ctapi.so
OBJS	= ctapi.o

all:: $(LIBS) $(APPS)

clean distclean::
	rm -rf .sobjs $(LIBS)

install: ;

ifd-ctapi.so: $(addprefix .sobjs/,$(OBJS))
	ld -shared -o $@ $(addprefix .sobjs/,$(OBJS)) -L../../lib -lifd

.sobjs/%.o: %.c
	@mkdir -p .sobjs
	$(CC) -fPIC $(CFLAGS) -c $< -o $@

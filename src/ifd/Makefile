DEBUG	= -Wall -g
CFLAGS	= -I../include $(DEBUG)
LIB	= libifd.a
OBJS	= manager.o device.o reader.o \
	  proto-t1.o \
	  checksum.o

all: $(LIB)

clean:
	rm -rf .objs *.a

libifd.a: $(addprefix .objs/,$(OBJS))
	ar cr $@ $<

.objs/%.o: %.c
	@mkdir -p .objs
	$(CC) $(CFLAGS) -c $< -o $@

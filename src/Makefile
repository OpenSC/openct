
SUBDIRS	= \
	ct \
	ifd \
	ctapi \
	pcsc \
	daemon \
	tools

all clean distclean install:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@; \
	done

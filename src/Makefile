
SUBDIRS	= \
	ct \
	ifd \
	ctapi \
	pcsc \
	control \
	tools

all clean distclean install:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@; \
	done


SUBDIRS	= \
	libcardreader \
	libifd \
	ctapi \
	pcsc \
	ifdd

all clean distclean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@; \
	done

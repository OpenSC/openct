
SUBDIRS	= ifd

all clean distclean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@; \
	done

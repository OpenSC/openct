
SUBDIRS	= openct

all clean distclean install:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@; \
	done

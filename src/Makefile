
SUBDIRS	= libifd ctapi

all clean distclean:
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@; \
	done

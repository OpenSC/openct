
SUBDIRS	= include src

all::
	@mkdir -p lib

all clean distclean::
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@; \
	done


SUBDIRS	= include src

all::
	@mkdir -p lib

all clean distclean install::
	for dir in $(SUBDIRS); do \
		$(MAKE) -C $$dir $@; \
	done

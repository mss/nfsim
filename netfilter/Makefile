NETFILTER_C_FILES=$(wildcard core/*.c)

all: dummy.o modules netfilter.o 

netfilter.o: $(TYPE)/$(TYPE).o gen/gen.o $(NETFILTER_C_FILES:.c=.o)
	ld -r -o $@ $^

gen/gen.o: gen/
	@cd $(@D) && $(MAKE) -f ../Makefile.kbuild obj=$(@F) $(@F)

$(TYPE)/$(TYPE).o: $(TYPE)/
	@cd $(@D) && $(MAKE) -f ../Makefile.kbuild obj=$(@F) $(@F)

modules:
	cd $(TYPE) && $(MAKE) -f ../Makefile.kbuild modules
	cd gen && $(MAKE) -f ../Makefile.kbuild modules

dummy.o:dummy.c
	$(COMPILE.c) $(GCOVFLAGS) -DKBUILD_MODNAME=$(@:.o=) $(OUTPUT_OPTION) $<

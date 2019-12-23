LLC  ?= clang
SINC ?= sinter

%.o: %.ll
	$(LLC) -c $(LLCFLAGS) -o $@ $<

%.ll: %.sin
	$(SINC) -l $(SINCFLAGS) -o $@ $<

%.dot: %.sin
	$(SINC) -g $(SINCFLAGS) -o $@ $<

%.svg: %.dot
	dot -Tsvg $< > $@

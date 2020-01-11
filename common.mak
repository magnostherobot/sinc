LLC  ?= llc
SINC ?= sinc

%.o: %.ll
	$(LLC) -filetype obj $(LLCFLAGS) -o $@ $<

%.ll: %.sin
	$(SINC) -l $(SINCFLAGS) -o $@ $<

%.dot: %.sin
	$(SINC) -g $(SINCFLAGS) -o $@ $<

%.svg: %.dot
	dot -Tsvg $< > $@

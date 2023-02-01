#LLC  ?= llc -filetype obj
LLC ?= clang -c
SINC ?= sinc

%.o: %.ll
	$(LLC) $(LLCFLAGS) -o $@ $<

%.ll: %.sin
	$(SINC) -l $(SINCFLAGS) -o $@ $<

%.dot: %.sin
	$(SINC) -g $(SINCFLAGS) -o $@ $<

%.svg: %.dot
	dot -Tsvg $< > $@

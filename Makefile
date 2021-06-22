pini: main.c libtmt/tmt.c config.h
	cc -o $@ main.c libtmt/tmt.c -lz

config.h:
	cp config.def.h config.h

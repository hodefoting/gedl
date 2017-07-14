gcut: *.c *.h Makefile default.edl.inc
	gcc *.c `pkg-config gegl-0.3 sdl mrg gexiv2 --cflags --libs` -Wall -g -o gcut -O2
clean:
	rm gcut
install: gcut
	install gcut /usr/local/bin

default.edl.inc: default.edl
	cat $< | \
	sed 's/\\/\\\\/g' | \
	sed 's/\r/a/' | \
	sed 's/"/\\"/g' | \
	sed 's/^/"/' | \
	sed 's/$$/\\n"/' > $@

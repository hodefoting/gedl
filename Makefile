gedl: *.c *.h Makefile default.edl.inc
	gcc *.c `pkg-config gegl-0.3 sdl mrg gexiv2 --cflags --libs` -Wall -g -o gedl -O2
clean:
	rm gedl
install: gedl
	install gedl /usr/local/bin

default.edl.inc: default.edl
	echo >> $@
	cat $< | \
	sed 's/\\/\\\\/g' | \
	sed 's/\r/a/' | \
	sed 's/"/\\"/g' | \
	sed 's/^/"/' | \
	sed 's/$$/\\n"/' >> $@

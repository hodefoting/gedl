gedl: *.c *.h Makefile
	gcc *.c `pkg-config gegl-0.3 mrg gexiv2 --cflags --libs` -O2 -Wall -p -g -o gedl
clean:
	rm gedl


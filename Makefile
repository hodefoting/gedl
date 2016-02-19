gedl: *.c *.h Makefile
	gcc -pg *.c `pkg-config gegl-0.3 mrg gexiv2 --cflags --libs` -O2 -Wall -o gedl
clean:
	rm gedl


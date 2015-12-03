gedl: *.c *.h
	gcc *.c `pkg-config gegl-0.3 mrg gexiv2 --cflags --libs` -O2 -Wall -o gedl
clean:
	rm gedl


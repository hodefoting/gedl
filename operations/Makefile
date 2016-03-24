all: tmedian.so tdiff.so

tmedian.so: tmedian.c Makefile
	gcc -O2 -fpic -shared -I. -o tmedian.so tmedian.c `pkg-config gegl-0.3 --cflags --libs`

tdiff.so: tdiff.c Makefile
	gcc -O2 -fpic -shared -I. -o tdiff.so tdiff.c `pkg-config gegl-0.3 --cflags --libs`
clean:
	rm *.so


gedl: *.c *.h Makefile
	gcc *.c `pkg-config gegl-0.3 mrg gexiv2 --cflags --libs` -O2 -Wall -g -o gedl
clean:
	rm gedl

SOUNDTRACK=TheBlackGoat_64kb.mp3
#SOUNDTRACK=HamboCorvette-Sweden-ViolaTurpeinensEnsStandardF5002b78-Couple-Advanced.mp3
#SOUNDTRACK=folk.mp3
#SOUNDTRACK=drmesno1.mp3

ngegl-audio.mp4: gegl.mp4 Makefile $(SOUNDTRACK)
	/usr/bin/ffmpeg -i gegl.mp4 -i $(SOUNDTRACK) -map 0:v -map 1:a -c:v copy -c:a copy gegl-audio.mp4

gegl-audio.mp4: gegl.mp4 Makefile $(SOUNDTRACK)
	/usr/bin/ffmpeg -i gegl.mp4 -i $(SOUNDTRACK) -map 0:v -map 1:a -c:v copy -c:a copy -shortest gegl-audio.mp4

gimp.mp4: Split-Goat.webm Makefile
	/usr/bin/ffmpeg -i Split-Goat.webm -filter:v "crop=1360:680:100:100" gimp.mp4

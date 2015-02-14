CFLAGS= -I/usr/include/SDL/
CFLAGS2= $(CFLAGS) -std=gnu99
LDFLAGS= -lyaml -lSDL -lm

build/linux/lsg-test: LSGcore.o LSGmlf.o LSGcmdbuffer.o
	g++ $(CFLAGS) $(LDFLAGS) -o build/linux/lsg-test ./LSGSDLtest/LSGSDLtest/main.cpp \
	                          ./LSGSDLtest/LSGSDLtest/MusicPreset.cpp \
	                          ./LSGTest/LSGcore/LSGsdl.c \
	                          LSGcore.o LSGmlf.o LSGcmdbuffer.o

LSGcmdbuffer.o:
	gcc $(CFLAGS2) $(LDFLAGS) -c -o LSGcmdbuffer.o ./LSGTest/LSGcore/LSGcmdbuffer.c

LSGcore.o:
	gcc $(CFLAGS2) $(LDFLAGS) -c -o LSGcore.o ./LSGTest/LSGcore/LSGcore.c

LSGmlf.o:
	gcc $(CFLAGS2) $(LDFLAGS) -c -o LSGmlf.o ./LSGTest/LSGcore/LSGmlf.c


all: cyclimb

CFLAGS=-g

textrender.o: textrender.cpp
	g++ -I/usr/include/freetype2 $(CFLAGS) $^ -c -o $@

chunk.o: chunk.cpp chunk.hpp
	g++ $(CFLAGS) $< -c -o $@ -O

game.o: game.cpp game.hpp
	g++ $(CFLAGS) $< -c -o $@

scene.o: scene.cpp scene.hpp
	g++ $(CFLAGS) $< -g -c -o $@

chunkindex.o: chunkindex.cpp chunkindex.hpp
	g++ $(CFLAGS) $< -c -o $@ -O2

gles/chunk.o: gles/chunk.cpp
	g++ $(CFLAGS) $^ -c -o $@ -O2

main.o: main.cpp
	g++ $(CFLAGS) $^ -c -o $@

%.o : %.cpp
	g++ $(CFLAGS) $^ -g -c -o $@


TARGETS=main.o testshapes.o shader.o camera.o \
	    testshapes.o shader.o camera.o chunk.o \
		util.o chunkindex.o sprite.o rendertarget.o \
		game.o textrender.o scene.o


cyclimb: $(TARGETS)
	g++ $(CFLAGS) $^ -o $@ -lGL -lGLEW -lglut -lGLU -lfreetype -lglfw

clean:
	@if [ -f cyclimb ]; then\
		rm -v cyclimb; \
	fi
	@for x in $(TARGETS); do if [ -f $$x ]; then rm -v $$x ; fi ; done
	@for x in $(TARGETS_GLES); do if [ -f $$x ]; then rm -v $$x ; fi ; done

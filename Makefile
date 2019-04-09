all: cyclimb

textrender.o: textrender.cpp
	g++ -I/usr/include/freetype2 $^ -c -o $@

chunk.o: chunk.cpp chunk.hpp
	g++ $< -c -o $@ -O2

game.o: game.cpp game.hpp
	g++ $< -c -o $@ -O2

scene.o: scene.cpp scene.hpp
	g++ $< -g -c -o $@

chunkindex.o: chunkindex.cpp chunkindex.hpp
	g++ $< -c -o $@ -O2

gles/chunk.o: gles/chunk.cpp
	g++ $^ -c -o $@ -O2

main.o: main.cpp
	g++ $^ -c -o $@

%.o : %.cpp
	g++ $^ -g -c -o $@


TARGETS=main.o testshapes.o shader.o camera.o \
	    testshapes.o shader.o camera.o chunk.o \
		util.o chunkindex.o sprite.o rendertarget.o \
		game.o textrender.o scene.o


cyclimb: $(TARGETS)
	g++ $^ -o $@ -lGL -lGLEW -lglut -lGLU -lfreetype -g

clean:
	@if [ -f cyclimb ]; then\
		rm -v cyclimb; \
	fi
	@for x in $(TARGETS); do if [ -f $$x ]; then rm -v $$x ; fi ; done
	@for x in $(TARGETS_GLES); do if [ -f $$x ]; then rm -v $$x ; fi ; done

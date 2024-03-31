LIBS=-L.\SDL2-2.30.1\i686-w64-mingw32\lib -lmingw32 -lSDL2main -lSDL2
INCLUDES=-I.\SDL2-2.30.1\i686-w64-mingw32\include\SDL2
CFLAGS=-std=c11 -Wall -Wextra -Werror
all:
	gcc chip8_interpretor.c -o chip8 $(CFLAGS) $(LIBS) $(INCLUDES)

debug:
	gcc chip8_interpretor.c -o chip8 -DDEBUG $(CFLAGS) $(LIBS) $(INCLUDES)
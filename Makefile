CC = gcc
CFLAGS = -Wall -std=c11
LDLIBS = -lrt -pthread

all: game wizard rogue barbarian

game: game.c dungeon_X86_64.o
	$(CC) $(CFLAGS) -o game game.c dungeon_X86_64.o $(LDLIBS)

wizard: wizard.c
	$(CC) $(CFLAGS) -o wizard wizard.c $(LDLIBS)

rogue: rogue.c
	$(CC) $(CFLAGS) -o rogue rogue.c $(LDLIBS)

barbarian: barbarian.c
	$(CC) $(CFLAGS) -o barbarian barbarian.c $(LDLIBS)

clean:
	rm -f game wizard rogue barbarian

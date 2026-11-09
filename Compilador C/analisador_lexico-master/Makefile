CFLAGS = -std=c17 -Wall -Wextra -pedantic -g

all: run

run: build
	@./main

build: main.o checkers.o
	@gcc -o main main.o checkers.o

main.o: main.c
	@gcc -o main.o main.c -c $(CFLAGS)

checkers.o: checkers.c
	@gcc -o checkers.o checkers.c -c $(CFLAGS)

clean:
	@rm -rf main main.o checkers.o

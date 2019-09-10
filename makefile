# makefile for ICT373 Assignment 2

main: main.o token.o command.o
	gcc -Wall main.o token.o command.o -o main -lm

main.o: main.c token.h command.h
	gcc -Wall -c main.c

token.o: token.c token.h
	gcc -Wall -c token.c
	
command.o: command.c command.h
	gcc -Wall -c command.c

clean:
	rm *.o

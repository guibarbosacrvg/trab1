.PHONY: all clean

all: prog1 prog2 prog3 prog4 main run

prog1: prog.c
	gcc -o prog1 prog.c

prog2: prog.c
	gcc -o prog2 prog.c

prog3: prog.c
	gcc -o prog3 prog.c

prog4: prog.c
	gcc -o prog4 prog.c

prog5: prog.c
	gcc -o prog5 prog.c

main: main.c
	gcc -o main main.c

run: main
	./main
clean:
	rm -f prog1 prog2 prog3 prog4 main

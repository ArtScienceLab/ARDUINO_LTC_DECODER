
all:
	gcc -c ltcdecode.c	-W -Wall -std=c11 -pedantic -O2
	gcc -c ltc.c		-W -Wall -std=c11 -pedantic -O2
	gcc -c decoder.c	-W -Wall -std=c11 -pedantic -O2
	gcc -c timecode.c	-W -Wall -std=c11 -pedantic -O2
	gcc -o ltcdecode *.o 	-lc -lm -ffast-math
	rm *.o

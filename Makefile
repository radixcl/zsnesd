CC = gcc
#CFLAGS = -g -Wall
CFLAGS = -O2 -Wall
LIBS = -lpthread

all:
	$(CC) $(CFLAGS) $(LIBS) main.c -o zsnesd

clean:
	rm -f zsnesd

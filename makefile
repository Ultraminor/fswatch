CC = gcc
SOURCES = main.c notify.c error.c
OBJECTS = $(SOURCES:.c=.o)
OUTPUT = fswatch
FLAGS = -Wall -O4

all: $(OUTPUT)

$(OUTPUT): $(OBJECTS)
	gcc -o $(OUTPUT) $(OBJECTS) $(FLAGS)

%.o: %.c
	$(CC) -c $< $(FLAGS)

clean:
	-rm $(OUTPUT) $(OBJECTS)

install: all
	cp $(OUTPUT) /usr/bin/$(OUTPUT)

uninstall:
	rm /usr/bin/$(OUTPUT)

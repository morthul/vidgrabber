SOURCES=vidgrabber.c
OBJECTS=$(SOURCES:.c=.o)
EXECUTABLE=vidgrabber

CFLAGS = -Werror -Wall

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
	CC = clang
	CFLAGS += -Weverything
else
	CC = gcc
	LIBS = -pthread
endif


all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(CFLAGS) $(LIBS) -o $@ $(OBJECTS)

clean:
	rm -rf $(EXECUTABLE) $(OBJECTS)

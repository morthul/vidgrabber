CXX = gcc
CFLAGS = -Werror -Wall
LIBS = -pthread

.c:
	$(CXX) $(CFLAGS) -c $<

SRC = vidgrabber.c
OBJ = $(addsuffix .o, $(basename $(SRC)))

vidgrabber: $(OBJ)
	$(CXX) $(CFLAGS) $(LIBS) -o $@ $(OBJ)

CXX = clang
CFLAGS = -Werror -Wall -Weverything

.c:
	$(CXX) $(CFLAGS) -c $<

SRC = vidgrabber.c
OBJ = $(addsuffix .o, $(basename $(SRC)))

vidgrabber: $(OBJ)
	$(CXX) $(CFLAGS) -o $@ $(OBJ)
#Makefile
LIBPREFIX = lib
MODULE    = mio

CFLAGS = -I.

LDFLAGS = -lpthread

CC = gcc

SRC_FILE = $(wildcard *.c)

OBJ_FILE = $(patsubst %.c,%.o,$(SRC_FILE)) 
              			
TARGET = $(LIBPREFIX)$(MODULE).so

all:$(TARGET)
                               
$(TARGET):$(OBJ_FILE)
	$(CC) -shared $^ $(LDFLAGS) -o $@
	@echo "Compile success..."
	
%.o:%.c
	$(CC) -fPIC -c $< $(CFLAGS) -o $@

.PHONY:clean
clean:
	@echo "clean begin..."
	-rm -f $(OBJ_FILE)
	-rm -f $(TARGET)
	@echo "clean success..."
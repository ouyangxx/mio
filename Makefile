#Makefile
LIBPREFIX = lib
MODULE    = mio

CFLAGS = -I.

LDFLAGS = -lpthread

CC = gcc

SRC_FILE = $(wildcard src/*.c compat/*.c)

OBJ_FILE = $(patsubst %.c,%.o,$(SRC_FILE)) 
              			
TARGET = $(LIBPREFIX)$(MODULE).so

all:$(TARGET)
                               
$(TARGET):$(OBJ_FILE)
	$(CC) -shared $^ $(LDFLAGS) -o $@
	-mv $(TARGET) lib
	@echo "Compile success..."
	
%.o:%.c
	$(CC) -fPIC -c $< $(CFLAGS) -o $@

.PHONY:clean
clean:
	@echo "clean begin..."
	-rm -f $(OBJ_FILE)
	-rm -f lib/$(TARGET)
	@echo "clean success..."
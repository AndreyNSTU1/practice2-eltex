CC = gcc
CFLAGS = -Wall -Wextra -std=c11
TARGET = mask
SRC = mask.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
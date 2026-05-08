CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
TARGET = pq
SRC = priority_queue.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

rebuild: clean all
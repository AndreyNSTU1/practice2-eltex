CC = gcc
CFLAGS = -Wall -Wextra -std=c11
TARGET = calculator

all: $(TARGET)

$(TARGET): calculator.c
	$(CC) $(CFLAGS) calculator.c -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
CC = gcc
CFLAGS = -Wall -Wextra -std=c11
TARGET = calculyator
SRC = calculyator.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
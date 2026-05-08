CC = gcc
CFLAGS = -Wall -Wextra -std=c11
TARGET = phonebook
SRC = phonebook.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
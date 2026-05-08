CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -g

TARGET = phonebook_new
SRC = phonebook_new.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
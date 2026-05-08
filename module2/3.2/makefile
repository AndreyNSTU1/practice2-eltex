CC = gcc
CFLAGS = -Wall -Wextra -O2

TARGET = ipcheck
SRC = ipcheck.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

run: $(TARGET)
	./$(TARGET) 192.168.1.1 255.255.255.0 20

clean:
	rm -f $(TARGET)
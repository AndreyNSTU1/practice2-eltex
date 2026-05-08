CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -fPIC

TARGET = app
LIB = libcontact.dll

all: $(TARGET)

# DLL вместо SO
$(LIB): contact.o
	$(CC) -shared -o $(LIB) contact.o

contact.o: contact.c contact.h
	$(CC) $(CFLAGS) -c contact.c

main.o: main.c contact.h
	$(CC) $(CFLAGS) -c main.c

$(TARGET): $(LIB) main.o
	$(CC) main.o -L. -lcontact -o $(TARGET)

clean:
	rm -f *.o *.dll *.exe
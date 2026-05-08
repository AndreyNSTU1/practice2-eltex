CC = gcc
CFLAGS = -Wall -Wextra -std=c11

TARGET = app
LIB = libcontact.a

OBJS = contact.o main.o

# ===== Сборка всего проекта =====
all: $(TARGET)

# ===== Исполняемый файл =====
$(TARGET): $(LIB) main.o
	$(CC) main.o -L. -lcontact -o $(TARGET)

# ===== Статическая библиотека =====
$(LIB): contact.o
	ar rcs $(LIB) contact.o

# ===== Компиляция contact.c =====
contact.o: contact.c contact.h
	$(CC) $(CFLAGS) -c contact.c -o contact.o

# ===== Компиляция main.c =====
main.o: main.c contact.h
	$(CC) $(CFLAGS) -c main.c -o main.o

# ===== Очистка =====
clean:
	rm -f *.o $(TARGET) $(LIB)
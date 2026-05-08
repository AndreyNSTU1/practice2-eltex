CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -shared
TARGET = calculator.exe
PLUGINS = add.dll sub1.dll mul.dll div.dll

all: $(TARGET) $(PLUGINS)

$(TARGET): calculator.c
	$(CC) $(CFLAGS) -o $@ $< -lwinmm

%.dll: %.c
	$(CC) $(LDFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	del $(TARGET) $(PLUGINS) 2>nul || rm -f $(TARGET) $(PLUGINS)

.PHONY: all clean run
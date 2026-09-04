CC = gcc
CFLAGS = -O2 -Wall -Wextra -std=c99
TARGET = minicraft3ds-font-json2bin
SRCS = main.c cJSON.c

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRCS) format.h cJSON.h
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET) *.o

test: $(TARGET)
	./$(TARGET) example_font.json /tmp/test_output.bin --verify

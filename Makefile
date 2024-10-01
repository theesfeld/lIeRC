CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lcurl -ljson-c -lncurses -lpthread

TARGET = lierc
SRCS = lierc.c

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean

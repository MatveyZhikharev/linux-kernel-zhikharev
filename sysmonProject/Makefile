CC = gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS =

TARGET = sysmon
SOURCES = sysmon.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJECTS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

install: $(TARGET)
	cp $(TARGET) /usr/local/bin/$(TARGET)

help:
	@echo "Available targets:"
	@echo "  make        - build sysmon"
	@echo "  make clean  - remove build artifacts"
	@echo "  make install - install to /usr/local/bin"

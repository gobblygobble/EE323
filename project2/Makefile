CC = gcc
CFLAGS = -Wall -g -std=gnu99

SOURCES = proxy.c
HEADERS = proxy.h
TARGET = proxy

.PHONY: all clean tags submit

objects = $(SOURCES:.c=.o)

proxy: $(objects) 
	$(CC) $(CFLAGS) -o $(TARGET) $(objects)

all: $(TARGET)

clean: 
	@rm -f *.o $(TARGET) 

tags:
	@ctags $(SOURCES)

submit: clean
	@tar zcf 20160609_assign2.tar.gz $(SOURCES) $(HEADERS) README Makefile

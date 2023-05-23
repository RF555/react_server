CC = gcc
CFLAGS = -Wall -Wextra -Werror -std=c11 -g -pedantic
SFLAGS = -shared
TFLAGS = -pthread
HFILE = st_reactor.h
LIBFILE = st_reactor.so
RM = rm -f

all: react_server

default: all

react_server: react_server.o $(LIBFILE)
	$(CC) $(CFLAGS) -o $@ $< ./$(LIBFILE) $(TFLAGS)

$(LIBFILE): st_reactor.o
	$(CC) $(CFLAGS) $(SFLAGS) -o $@ $^ $(TFLAGS)

st_reactor.o: st_reactor.c $(HFILE)
	$(CC) $(CFLAGS) -fPIC -c $<

%.o: %.c $(HFILE)
	$(CC) $(CFLAGS) -c $<

clean:
	$(RM) *.o *.so react_server
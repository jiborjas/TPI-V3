# Tests de host: compilan la logica del firmware en la PC
CC = gcc
CFLAGS = -Wall -Wextra -O0 -g -I../firmware

SRC = test_host.c \
      ../firmware/protocol/protocol.c \
      ../firmware/protocol/parser.c \
      ../firmware/app/command_box.c \
      ../firmware/app/signals.c

test_host: $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

.PHONY: run sim fsm clean
run: test_host
	./test_host

sim: test_host
	./test_host --sim

fsm: test_host
	./test_host --fsm

clean:
	rm -f test_host

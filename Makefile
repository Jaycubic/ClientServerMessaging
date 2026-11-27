CC      ?= gcc
CFLAGS  ?= -std=gnu11 -Wall -Wextra -Werror -O2 -g
SAN     ?=
CFLAGS  += -Isrc -pthread $(SAN)
LDFLAGS += -pthread $(SAN)

BUILD   := build
LIB_SRC := src/net.c src/session.c src/log.c src/users.c src/commands.c \
           src/db_stub.c src/client_handler.c
LIB_OBJ := $(LIB_SRC:src/%.c=$(BUILD)/%.o)

UNIT_TESTS := $(BUILD)/test_net $(BUILD)/test_session $(BUILD)/test_commands

.PHONY: all test unit integration tsan asan clean

all: $(BUILD)/server

$(BUILD)/%.o: src/%.c $(wildcard src/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/server: src/server.c $(LIB_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD)/test_%: tests/test_%.c $(LIB_OBJ) $(wildcard tests/*.h)
	$(CC) $(CFLAGS) -Itests $< $(LIB_OBJ) -o $@ $(LDFLAGS)

$(BUILD):
	mkdir -p $(BUILD)

unit: $(UNIT_TESTS)
	@for t in $(UNIT_TESTS); do echo "== $$t"; $$t || exit 1; done

integration: $(BUILD)/server
	python3 tests/integration.py -v

test: unit integration

# Rebuild everything with a sanitizer and run the whole suite.
tsan:
	$(MAKE) clean
	$(MAKE) SAN=-fsanitize=thread test
asan:
	$(MAKE) clean
	$(MAKE) SAN="-fsanitize=address,undefined -fno-omit-frame-pointer" test

clean:
	rm -rf $(BUILD)

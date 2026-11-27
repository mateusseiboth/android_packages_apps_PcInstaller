# Host build and tests for pc-installd. The device build uses Android.bp.
#   make test     build and run every tests/test_*.c
#   make          build the host binary (useful for --help and the socket protocol by hand)

CC ?= cc
CFLAGS ?= -O1 -g -Wall -Wextra -Werror -std=gnu11
CFLAGS += -Isrc
LDLIBS += -lcrypto -ldl
BUILD := build
CORE := $(filter-out src/main.c,$(wildcard src/*.c))
TESTS := $(patsubst tests/%.c,$(BUILD)/%,$(wildcard tests/test_*.c))

all: $(BUILD)/pc-installd

$(BUILD):
	mkdir -p $@

$(BUILD)/pc-installd: src/main.c $(CORE) $(wildcard src/*.h) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ src/main.c $(CORE) $(LDLIBS)

$(BUILD)/test_%: tests/test_%.c $(CORE) $(wildcard src/*.h) tests/check.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(CORE) $(LDLIBS)

test: $(TESTS) $(BUILD)/pc-installd
	@set -e; for t in $(TESTS); do echo "== $$t"; $$t; done
	python3 -m unittest discover -s tests -p 'test_*.py'

clean:
	rm -rf $(BUILD)

.PHONY: all test clean

# DNS resolver - C, no resolver library. POSIX sockets/time only.

CC      ?= cc
STD      = -std=c11
WARN     = -Wall -Wextra
# Feature-test macros so C11 mode still exposes POSIX socket/time APIs.
DEFS     = -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE
CFLAGS  ?= -O2
ALLCFLAGS = $(STD) $(WARN) $(DEFS) $(CFLAGS)

ASAN     = -fsanitize=address -g -O1

SRC_DIR  = src
SRCS     = $(SRC_DIR)/main.c $(SRC_DIR)/resolve.c $(SRC_DIR)/dns_query.c \
           $(SRC_DIR)/dns_encode.c $(SRC_DIR)/dns_decode.c \
           $(SRC_DIR)/cache.c $(SRC_DIR)/root_hints.c

BIN      = resolve

.PHONY: all test clean

all: $(BIN)

$(BIN): $(SRCS)
	$(CC) $(ALLCFLAGS) -o $@ $(SRCS)

# Unit tests built with AddressSanitizer (bounds/UAF detection).
test: test_message test_cache test_cname
	@echo "== running test_message =="
	./test_message
	@echo "== running test_cache =="
	./test_cache
	@echo "== running test_cname (offline CNAME termination) =="
	./test_cname

test_message: tests/test_message.c $(SRC_DIR)/dns_encode.c $(SRC_DIR)/dns_decode.c
	$(CC) $(STD) $(WARN) $(DEFS) $(ASAN) -o $@ \
	    tests/test_message.c $(SRC_DIR)/dns_encode.c $(SRC_DIR)/dns_decode.c

test_cache: tests/test_cache.c $(SRC_DIR)/cache.c
	$(CC) $(STD) $(WARN) $(DEFS) $(ASAN) -o $@ \
	    tests/test_cache.c $(SRC_DIR)/cache.c

# CNAME termination test drives resolve() purely from cache (no network).
CNAME_SRCS = $(SRC_DIR)/resolve.c $(SRC_DIR)/dns_query.c \
             $(SRC_DIR)/dns_encode.c $(SRC_DIR)/dns_decode.c \
             $(SRC_DIR)/cache.c $(SRC_DIR)/root_hints.c
test_cname: tests/test_cname.c $(CNAME_SRCS)
	$(CC) $(STD) $(WARN) $(DEFS) $(ASAN) -o $@ \
	    tests/test_cname.c $(CNAME_SRCS)

clean:
	rm -f $(BIN) test_message test_cache test_cname
	rm -rf *.dSYM

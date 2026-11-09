CC      ?= cc
AR      ?= ar
CFLAGS  ?= -std=gnu11 -Wall -Wextra -Wpedantic -O2 -g -Iinclude
LDLIBS  := -lm

LIB_SRCS := src/envelope.c src/edgemap.c
LIB_OBJS := $(LIB_SRCS:.c=.o)
LIB      := liboscviz.a

BIN      := oscviz
MAIN_OBJ := src/main.o

TEST_SRC := tests/test_edgemap.c
TEST_BIN := tests/test_edgemap

.PHONY: all clean test

all: $(LIB) $(BIN)

$(LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^

$(BIN): $(MAIN_OBJ) $(LIB)
	$(CC) $(CFLAGS) -o $@ $(MAIN_OBJ) -L. -loscviz $(LDLIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TEST_BIN): $(TEST_SRC) $(LIB)
	$(CC) $(CFLAGS) -o $@ $(TEST_SRC) -L. -loscviz $(LDLIBS)

test: $(TEST_BIN)
	valgrind --error-exitcode=1 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all ./$(TEST_BIN)

clean:
	rm -f $(LIB_OBJS) $(MAIN_OBJ) $(LIB) $(BIN) $(TEST_BIN)

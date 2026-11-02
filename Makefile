CC = cc
CPPFLAGS = -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Iinc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -g -O0
LDLIBS = -pthread -lsqlite3
SOURCES = main logging logger_proc sbuffer protocol thread_workers connection_manager data_manager storage_manager
OBJECTS = $(addprefix build/,$(addsuffix .o,$(SOURCES)))

.PHONY: all clean test debug sanitize
all: gateway sensor_node

gateway: $(OBJECTS)
	$(CC) $(CFLAGS) $^ $(LDLIBS) -o $@

sensor_node: src/sensor_node.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

build/%.o: src/%.c
	mkdir -p build
	$(CC) $(CPPFLAGS) $(CFLAGS) -pthread -MMD -MP -c $< -o $@

build/test_sbuffer: tests/test_sbuffer.c build/sbuffer.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -pthread -o $@

build/test_protocol: tests/test_protocol.c build/protocol.o
	$(CC) $(CPPFLAGS) $(CFLAGS) $^ -o $@

test: all build/test_sbuffer build/test_protocol
	./build/test_sbuffer
	./build/test_protocol
	python3 tests/test_integration.py

debug:
	$(MAKE) clean
	$(MAKE) CPPFLAGS='$(CPPFLAGS) -DDEBUG' all

sanitize:
	$(MAKE) clean
	$(MAKE) CFLAGS='-std=c11 -Wall -Wextra -Wpedantic -Werror -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer' test

clean:
	rm -rf build gateway sensor_node gateway.dSYM sensor_node.dSYM

-include $(OBJECTS:.o=.d)

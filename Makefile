SFLAGS:=--std=gnu99
WFLAGS:=-Wall -Werror -pedantic
BINARY:=umtsd
SOURCES:=$(wildcard src/*.c)
HEADERS:=$(wildcard src/*.h)

# This will be renamed to json-c in version 0.11
JSON_LIB:=json

# Allow locally setting CFLAGS etc, which is useful during development.
-include Makefile.local

all: $(BINARY)

$(BINARY): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SFLAGS) $(WFLAGS) $(LDFLAGS) -l$(JSON_LIB) -lubox -luci -o $@ $(SOURCES)

clean:
	rm -f $(BINARY)

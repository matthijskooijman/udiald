SFLAGS:=--std=gnu99
WFLAGS:=-Wall -Werror -pedantic
BINARY:=umtsd
SOURCES:=$(wildcard src/*.c)
HEADERS:=$(wildcard src/*.h)

all: $(BINARY)

$(BINARY): $(SOURCES) $(HEADERS)
	$(CC) $(CFLAGS) $(SFLAGS) $(WFLAGS) $(LDFLAGS) -lubox -luci -o $@ $(SOURCES)

clean:
	rm -f $(BINARY)

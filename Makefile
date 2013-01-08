SFLAGS:=--std=gnu99
WFLAGS:=-Wall -Werror -pedantic
BINARY:=umtsd

all: $(BINARY)

$(BINARY): src/*.c
	$(CC) $(CFLAGS) $(SFLAGS) $(WFLAGS) $(LDFLAGS) -lubox -luci -o $@ $+

clean:
	rm -f $(BINARY)

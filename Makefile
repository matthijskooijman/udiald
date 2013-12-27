SFLAGS:=--std=gnu99
WFLAGS:=-Wall -Werror -pedantic
BINARY:=udiald
SOURCES:=$(wildcard src/*.c)
HEADERS:=$(wildcard src/*.h)
DEVICE_CONFIG_HUAWEI:=src/deviceconfig_huawei.h

# Allow locally setting CFLAGS etc, which is useful during development.
-include Makefile.local

all: $(BINARY)

$(BINARY): $(SOURCES) $(HEADERS) $(DEVICE_CONFIG_HUAWEI)
	$(CC) $(CFLAGS) $(SFLAGS) $(WFLAGS) $(LDFLAGS) -ljson-c -lubox -luci -o $@ $(SOURCES)

$(DEVICE_CONFIG_HUAWEI): data/50-Huawei-Datacard.rules data/extract-huawei.py
	data/extract-huawei.py < $< > $@

clean:
	rm -f $(BINARY) $(DEVICE_CONFIG_HUAWEI)

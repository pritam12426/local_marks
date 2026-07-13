# Makefile — local-mark build system
#
# Targets:
#   all     — release build (-O3)
#   debug   — debug build (-g3 -DDEBUG)
#   strip   — strip debug symbols
#   install — install to PREFIX
#   uninstall
#   clean
#
# Options (set via environment or on the command line):
#   make debug -B O_DEBUG=1  — debug build
#
# macOS prerequisite: brew install argp-standalone

UNAME_S := $(shell uname -s)

PREFIX ?= /usr/local
MANPREFIX ?= $(PREFIX)/share/man

STRIP ?= strip
INSTALL ?= install

BUILD       =  build
MARKS2JSON  =  marks2json.py
BIN         =  local-mark

HEADERS   = $(wildcard src/*.h)
SRC      := $(wildcard src/*.c)

FRONT_END_FILES = \
    front_end/javascript/browse.js \
    front_end/javascript/data.js \
    front_end/javascript/health.js \
    front_end/javascript/info.js \
    front_end/javascript/keyboard.js \
    front_end/javascript/main.js \
    front_end/javascript/panel.js \
    front_end/javascript/random.js \
    front_end/javascript/search.js \
    front_end/javascript/sidebar.js \
    front_end/javascript/tag_bar.js \
    front_end/stylesheet/style.css \
    front_end/stylesheet/themes/light.css \
    front_end/favicon.ico \
    front_end/index.html \
    front_end/sw.js

FRONT_END_SCRIPT       = front_end/embed_frontend.bash
FRONT_END_GENERATED_C  = $(BUILD)/gen_embedded_front_end_dir.c
FRONT_END_GENERATED_H  = src/gen_embedded_front_end_dir.h
FRONT_END_GENERATED_O  = $(BUILD)/gen_embedded_front_end_dir.o

# Compiler warnings
CFLAGS +=  -Wall -Wextra -Wpedantic \
           -Wstrict-prototypes -Wmissing-prototypes \
           -Wshadow -Wconversion \
           -Wno-missing-field-initializers

# Common flags
CFLAGS += -Isrc -std=c17 -DLOG_SHOW_TIME_STAMP
LDLIBS +=  -lpthread

# Convert targets to flags for backwards compatibility
O_DEBUG := 0  # Debug binary (0 = release, 1 = debug)

ifneq ($(filter debug,$(MAKECMDGOALS)),)
	O_DEBUG := 1
endif

ifeq ($(strip $(O_DEBUG)),1)
	CFLAGS += -g3 -DDEBUG -DLOG_SHOW_SOURCE_LOCATION

	LDFLAGS += -fsanitize=address -fsanitize=undefined
	CFLAGS += -fstack-usage \
	          -fsanitize=address \
	          -fsanitize=undefined

    ifneq (,$(findstring clang,$(CC)))
		CFLAGS += -ffreestanding
    endif
else
	CFLAGS += -O3
endif

# Platform-specific settings
ifeq ($(UNAME_S),Darwin)
	# macOS: need argp from Homebrew (brew install argp-standalone)
	LDLIBS += -largp
else
	# Linux: _GNU_SOURCE for strptime, etc.
	CFLAGS += -D_GNU_SOURCE
endif

OUT = $(SRC:%.c=$(BUILD)/%.o)

all: $(BIN)

help: ## Show this help
	@grep -E '^[a-zA-Z_-]+:.*?## .*$$' $(MAKEFILE_LIST) | \
	awk 'BEGIN {FS = ":.*?## "}; {printf "\033[33m%-20s\033[0m %s\n", $$1, $$2}'

$(BUILD): ## Create build directories automatically
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c $(FRONT_END_GENERATED_H)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# Regenerate when any frontend file or the script itself changes
$(FRONT_END_GENERATED_C) $(FRONT_END_GENERATED_H): $(FRONT_END_FILES) $(FRONT_END_SCRIPT)
	@OUT_C_FILE="$(FRONT_END_GENERATED_C)" \
	OUT_H_FILE="$(FRONT_END_GENERATED_H)" \
	TARGET_FILES="$(FRONT_END_FILES)" \
	bash $(FRONT_END_SCRIPT)

$(FRONT_END_GENERATED_O): $(FRONT_END_GENERATED_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $(FRONT_END_GENERATED_C) -o $@

$(BIN): $(OUT) $(FRONT_END_GENERATED_O) ## Build the local-mark binary
	$(CC) $(LDFLAGS) -o $@ $(OUT) $(FRONT_END_GENERATED_O) $(LDLIBS)

debug: $(BIN) ## Build the debug binary run `make debug -B O_DEBUG=1`

install: all ## Install the local-mark binary
	$(INSTALL) -m 0755 -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m 0755 $(MARKS2JSON) $(DESTDIR)$(PREFIX)/bin/marks2json

clean: ## Clean up build artifacts
	$(RM) -rf $(OUT) $(BIN) $(FRONT_END_GENERATED_C) $(FRONT_END_GENERATED_H)

uninstall: ## Uninstall the local-mark binary
	$(RM) $(DESTDIR)$(PREFIX)/bin/$(BIN)

strip: $(BIN) ## Strip the local-mark binary
	$(STRIP) $^

.PHONY: all install uninstall strip clean debug

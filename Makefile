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
CURL ?= curl -fsSL

# Convert targets to flags for backwards compatibility
O_DEBUG := 0  ## Debug binary (1 = debug,   0 = release)
O_TLS   := 0  ## Support TLS  (1 = enable,  0 = disable)

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

BUILD       =  build
MARKS2JSON  =  marks2json.py
BIN         =  local-mark

# TLS support (tlse library)
TLS_DIR     =  third_party/eduardsui_tlse
TLS_GITHUB  =  https://raw.githubusercontent.com/eduardsui/tlse/refs/tags/v1.0.7

# cJson support (tlse library)
CJSON_DIR     =  third_party/cJson
CJSON_GITHUB  = https://raw.githubusercontent.com/DaveGamble/cJSON/refs/tags/v1.7.19


FRONT_END_FILES = \
    front_end/javascript/browse.js \
    front_end/javascript/data.js \
    front_end/javascript/databases.js \
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
    front_end/index.html

FRONT_END_SCRIPT       = front_end/embed_frontend.bash
FRONT_END_GENERATED_C  = $(BUILD)/gen_embedded_front_end_dir.c
FRONT_END_GENERATED_H  = src/gen_embedded_front_end_dir.h
FRONT_END_GENERATED_O  = $(BUILD)/gen_embedded_front_end_dir.o

# Compiler warnings
CFLAGS +=  -Wshadow -Wconversion \
           -Wall -Wextra -Wpedantic \
           -Wno-missing-field-initializers \
           -Wstrict-prototypes -Wmissing-prototypes

# Common flags
CFLAGS += -Isrc -std=c17 -DLOG_SHOW_TIME_STAMP
LDLIBS +=  -lpthread

# Source files
HEADERS   = $(wildcard src/*.h)
SRC      := $(wildcard src/*.c)
OUT       = $(SRC:%.c=$(BUILD)/%.o)
DEP       = $(OUT:.o=.d)

# TLS flags + objects (appended to OUT when enabled)
TLS_HEADER_DEP :=
ifeq ($(strip $(O_TLS)),1)
	CFLAGS         += -DSUPPORT_TLS_E -I$(TLS_DIR)
	OUT            += $(BUILD)/$(TLS_DIR)/tlse.o
	TLS_HEADER_DEP := $(TLS_DIR)/tlse.h

    ifeq ($(strip $(O_DEBUG)),1)
		CFLAGS += -DLTM_DESC
    endif
endif


all: $(BIN)

help:  ## Show this help
	@echo "Variable:"
	@awk 'BEGIN {FS="  ## "} \
		/^O_[a-zA-Z_]+[[:space:]]*:=/ { \
		split($$1, a, /[[:space:]]*:=/); \
		printf "  \033[36m%-20s\033[0m %s\n", a[1], $$2; \
	}' $(MAKEFILE_LIST)

	@echo
	@echo "Targets:"
	@grep -hE '^[a-zA-Z_-]+:.*  ## ' $(MAKEFILE_LIST) | \
	awk 'BEGIN {FS="  ## "}; {printf "  \033[33m%-20s\033[0m %s\n", $$1, $$2}'

	@echo
	@echo "TLS Examples:"
	@echo "  make tls                  # Build with TLS support"
	@echo "  make tls O_DEBUG=1        # Debug build with TLS"
	@echo "  make download-tls         # Download TLS files only"

$(BUILD):  ## Create build directories automatically
	mkdir -p $(BUILD)

$(BUILD)/%.o: %.c | $(FRONT_END_GENERATED_H) $(TLS_HEADER_DEP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Pull in the compiler-generated per-file header dependencies (created above
# via -MMD -MP). This is what makes "only the .c file(s) that actually
# #include gen_embedded_front_end_dir.h get rebuilt when it changes" true —
# without it, the order-only prerequisite above would correctly avoid a full
# rebuild, but the one real consumer wouldn't rebuild either.
-include $(DEP)

# Regenerate when any frontend file or the script itself changes
$(FRONT_END_GENERATED_C): $(FRONT_END_FILES) $(FRONT_END_SCRIPT)
	@OUT_C_FILE="$(FRONT_END_GENERATED_C)" \
	OUT_H_FILE="$(FRONT_END_GENERATED_H)" \
	TARGET_FILES="$(FRONT_END_FILES)" \
	bash $(FRONT_END_SCRIPT)

$(FRONT_END_GENERATED_H): $(FRONT_END_GENERATED_C)
	@# Header is generated as side-effect of .c generation

$(FRONT_END_GENERATED_O): $(FRONT_END_GENERATED_C)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $(FRONT_END_GENERATED_C) -o $@

# ── TLS support rules ────────────────────────────────────────────────────────

$(TLS_DIR):
	mkdir -p $(TLS_DIR)

$(TLS_DIR)/libtomcrypt.c: | $(TLS_DIR)
	$(CURL) -o $@ $(TLS_GITHUB)/libtomcrypt.c

$(TLS_DIR)/tlse.c: | $(TLS_DIR)
	$(CURL) -o $@ $(TLS_GITHUB)/tlse.c

$(TLS_DIR)/tlse.h: | $(TLS_DIR)
	$(CURL) -o $@ $(TLS_GITHUB)/tlse.h

# Explicit rule (not pattern) so the generic $(BUILD)/%.o rule can't shadow it
$(BUILD)/$(TLS_DIR)/tlse.o: $(TLS_DIR)/tlse.c $(TLS_DIR)/libtomcrypt.c $(TLS_DIR)/tlse.h | $(BUILD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DTLS_AMALGAMATION -w -MMD -MP -c $(TLS_DIR)/tlse.c -o $@

# Download TLS files only
download-tls:  ## Download the tlse sources into third_party/ without building
download-tls: $(TLS_DIR)/libtomcrypt.c $(TLS_DIR)/tlse.c $(TLS_DIR)/tlse.h
	@echo "TLS files downloaded to $(TLS_DIR)/"

clean-tls:  ## Remove downloaded TLS sources and their build objects
	$(RM) -rf $(TLS_DIR) $(BUILD)/$(TLS_DIR)

# ── Build targets ────────────────────────────────────────────────────────────

$(BIN): $(OUT) $(FRONT_END_GENERATED_O) ## Build the local-mark binary
	$(CC) $(LDFLAGS) -o $@ $(OUT) $(FRONT_END_GENERATED_O) $(LDLIBS)

debug:  ## Build the debug binary — run `make debug`
	$(MAKE) $(BIN) O_DEBUG=1

tls:  ## Build with TLS support (downloads tlse into third_party/ on first run) — run `make tls`
	$(MAKE) $(BIN) O_TLS=1

install: all  ## Install the local-mark binary
	$(INSTALL) -m 0755 -d $(DESTDIR)$(PREFIX)/bin
	$(INSTALL) -m 0755 $(BIN) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	$(INSTALL) -m 0755 $(MARKS2JSON) $(DESTDIR)$(PREFIX)/bin/marks2json

	$(INSTALL) -m 0755 -d $(DESTDIR)$(MANPREFIX)/man1
	$(INSTALL) -m 0755 local-mark.1 $(DESTDIR)$(MANPREFIX)/man1/$(BIN).1

clean:  ## Clean up build artifacts
	$(RM) -rf $(OUT) $(DEP) $(BIN) $(FRONT_END_GENERATED_C) $(FRONT_END_GENERATED_H) $(BUILD)/$(TLS_DIR)

uninstall:  ## Uninstall the local-mark binary
	$(RM) $(DESTDIR)$(PREFIX)/bin/$(BIN)
	$(RM) $(DESTDIR)$(PREFIX)/bin/marks2json
	$(RM)$(DESTDIR)$(MANPREFIX)/man1/$(BIN).1

strip: $(BIN)  ## Strip the local-mark binary
	$(STRIP) $^

.PHONY: all help install uninstall strip clean debug tls clean-tls download-tls

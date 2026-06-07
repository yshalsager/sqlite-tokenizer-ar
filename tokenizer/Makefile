.PHONY: build test fuzz random_parity random_parity_sweep verify_assets clean

CC ?= cc
CFLAGS ?= -O2 -fPIC -Wall -Wextra
UNAME_S := $(shell uname -s)
BREW_SQLITE_PREFIX := $(shell command -v brew >/dev/null 2>&1 && brew --prefix sqlite 2>/dev/null)
ifeq ($(UNAME_S),Darwin)
ifneq ($(BREW_SQLITE_PREFIX),)
SQLITE_CFLAGS ?= -I$(BREW_SQLITE_PREFIX)/include
else
SQLITE_CFLAGS ?= $(shell pkg-config --cflags sqlite3 2>/dev/null)
endif
else
SQLITE_CFLAGS ?= $(shell pkg-config --cflags sqlite3 2>/dev/null)
endif
ifeq ($(UNAME_S),Darwin)
LDFLAGS ?= -dynamiclib -undefined dynamic_lookup
else
LDFLAGS ?= -shared
endif
# Loadable SQLite extensions should not link libsqlite3 directly.
ifeq ($(UNAME_S),Darwin)
LDLIBS ?=
else
LDLIBS ?= -lm
endif

BUILD_DIR := build
SRC := src/sqlite_tokenizer_ar.c
INC := $(shell find src -name '*.inc' -type f 2>/dev/null)
OUT := $(BUILD_DIR)/sqlite_tokenizer_ar.so

build: $(OUT)

$(OUT): $(SRC) $(INC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SQLITE_CFLAGS) $(SRC) $(LDFLAGS) $(LDLIBS) -o $(OUT)

test: build
	@./scripts/verify_assets.sh
	@python3 ./scripts/smoke_test.py
	@python3 ./scripts/test_lucene_ported_cases.py
	@python3 ./scripts/fuzz_test.py --iterations 2000 --max-len 280 --seed 99

fuzz: build
	@python3 ./scripts/fuzz_test.py

random_parity: build
	@python3 ./scripts/test_lucene_random_parity.py --strict --check-positions

random_parity_sweep: build
	@for seed in 1 2 3 4 5 6 7 8 9 10; do \
		echo "seed=$$seed"; \
		python3 ./scripts/test_lucene_random_parity.py --cases 1000 --seed $$seed --max-len 160 --strict --check-positions; \
	done

verify_assets:
	@./scripts/verify_assets.sh

clean:
	rm -rf $(BUILD_DIR)

.PHONY: build test test_query public_smoke playground_verify clean

PYTHON_RUN ?= python3

build:
	@$(MAKE) -C tokenizer build
	@$(MAKE) -C query_compat build

test: public_smoke

public_smoke:
	@$(MAKE) -C tokenizer test
	@$(MAKE) -C query_compat public_test

test_query:
	@$(MAKE) -C query_compat public_test

playground_verify:
	@./playground/scripts/verify_custom_wasm.sh

clean:
	@$(MAKE) -C tokenizer clean
	@$(MAKE) -C query_compat clean

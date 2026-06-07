.PHONY: build test public_test backend_parity_gate compile_backend_parity_gate ast_backend_parity_gate ranking_backend_parity_gate ast_baseline_refresh compile_baseline_refresh expansion_strict_baseline_refresh ranking_baseline_refresh clean

PYTHON_RUN ?= python3
BACKEND_PARITY_DB ?=
BACKEND_PARITY_QUERIES ?= ../tests/fixtures/queries/inputs.smoke.jsonl
BACKEND_PARITY_FIELD ?= both
BACKEND_PARITY_LIMIT ?= 20
BACKEND_PARITY_LEFT ?= python
BACKEND_PARITY_RIGHT ?= c
AST_PARITY_DB ?=
AST_PARITY_QUERIES ?= ../tests/fixtures/queries/inputs.smoke.jsonl
AST_PARITY_LEFT ?= python
AST_PARITY_RIGHT ?= c
COMPILE_PARITY_DB ?=
COMPILE_PARITY_INPUTS ?= ../tests/fixtures/queries/compile.core.inputs.jsonl
COMPILE_PARITY_LEFT ?= python
COMPILE_PARITY_RIGHT ?= c
RANKING_PARITY_DB ?=
RANKING_PARITY_INPUTS ?= ../tests/fixtures/queries/ranking.core.inputs.jsonl
RANKING_PARITY_LEFT ?= python
RANKING_PARITY_RIGHT ?= c
RANKING_SCORE_TOLERANCE ?= 1e-6

build:
	@$(PYTHON_RUN) ./scripts/sqlite_query_compat.py --help >/dev/null
	@echo 'ok: query compat CLI scaffold is available'

test: public_test

public_test:
	@$(PYTHON_RUN) ./scripts/test_query_parser_roundtrip.py
	@$(PYTHON_RUN) ./scripts/test_ast_baseline.py
	@$(PYTHON_RUN) ./scripts/test_compile_baseline.py
	@$(PYTHON_RUN) ./scripts/test_expansion_strict_baseline.py
	@$(PYTHON_RUN) ./scripts/test_query_backend_gate.py
	@$(PYTHON_RUN) ./scripts/test_ast_c_simple_shapes.py
	@$(PYTHON_RUN) ./scripts/test_ast_c_fixture_coverage.py
	@$(PYTHON_RUN) ./scripts/test_ast_c_runtime_gap.py
	@$(PYTHON_RUN) ./scripts/test_ast_leaves_udf.py
	@$(PYTHON_RUN) ./scripts/test_ast_udf_internal_parity.py
	@$(PYTHON_RUN) ./scripts/test_ast_udf_fallback_policy.py
	@$(PYTHON_RUN) ./scripts/test_udf_missing_strict_policy.py
	@$(PYTHON_RUN) ./scripts/test_ast_backend_parity_gate.py
	@$(PYTHON_RUN) ./scripts/test_backend_parity_gate.py
	@$(PYTHON_RUN) ./scripts/test_compile_backend_parity_gate.py
	@$(PYTHON_RUN) ./scripts/test_execute_query_c_simple_term.py
	@$(PYTHON_RUN) ./scripts/test_execute_c_fixture_coverage.py
	@$(PYTHON_RUN) ./scripts/test_cli_default_backend.py
	@$(PYTHON_RUN) ./scripts/test_ranking_baseline.py
	@$(PYTHON_RUN) ./scripts/test_ranking_backend_parity_gate.py
	@$(PYTHON_RUN) ./scripts/test_norm_helpers_udf.py
	@$(PYTHON_RUN) ./scripts/test_token_helpers_udf.py
	@$(PYTHON_RUN) ./scripts/test_runtime_reference_wrapper_routing.py
	@$(PYTHON_RUN) ./scripts/test_runtime_reference_cleanup_guard.py
	@$(PYTHON_RUN) ./scripts/test_query_compat.py

backend_parity_gate:
	@if [ -z "$(BACKEND_PARITY_DB)" ]; then echo 'error: BACKEND_PARITY_DB is required'; exit 1; fi
	@$(PYTHON_RUN) ./scripts/run_backend_parity_gate.py \
		--db "$(BACKEND_PARITY_DB)" \
		--queries "$(BACKEND_PARITY_QUERIES)" \
		--field "$(BACKEND_PARITY_FIELD)" \
		--limit "$(BACKEND_PARITY_LIMIT)" \
		--left-backend "$(BACKEND_PARITY_LEFT)" \
		--right-backend "$(BACKEND_PARITY_RIGHT)"

compile_backend_parity_gate:
	@if [ -z "$(COMPILE_PARITY_DB)" ]; then echo 'error: COMPILE_PARITY_DB is required'; exit 1; fi
	@$(PYTHON_RUN) ./scripts/run_compile_backend_parity_gate.py \
		--db "$(COMPILE_PARITY_DB)" \
		--inputs "$(COMPILE_PARITY_INPUTS)" \
		--left-backend "$(COMPILE_PARITY_LEFT)" \
		--right-backend "$(COMPILE_PARITY_RIGHT)"

ranking_backend_parity_gate:
	@if [ -z "$(RANKING_PARITY_DB)" ]; then echo 'error: RANKING_PARITY_DB is required'; exit 1; fi
	@$(PYTHON_RUN) ./scripts/run_ranking_backend_parity_gate.py \
		--db "$(RANKING_PARITY_DB)" \
		--inputs "$(RANKING_PARITY_INPUTS)" \
		--left-backend "$(RANKING_PARITY_LEFT)" \
		--right-backend "$(RANKING_PARITY_RIGHT)" \
		--score-tolerance "$(RANKING_SCORE_TOLERANCE)"

ast_backend_parity_gate:
	@if [ -z "$(AST_PARITY_DB)" ]; then echo 'error: AST_PARITY_DB is required'; exit 1; fi
	@$(PYTHON_RUN) ./scripts/run_ast_backend_parity_gate.py \
		--db "$(AST_PARITY_DB)" \
		--queries "$(AST_PARITY_QUERIES)" \
		--left-backend "$(AST_PARITY_LEFT)" \
		--right-backend "$(AST_PARITY_RIGHT)"

ast_baseline_refresh:
	@$(PYTHON_RUN) ./scripts/generate_ast_baseline.py

compile_baseline_refresh:
	@$(PYTHON_RUN) ./scripts/generate_compile_baseline.py

expansion_strict_baseline_refresh:
	@$(PYTHON_RUN) ./scripts/generate_expansion_strict_baseline.py

ranking_baseline_refresh:
	@$(PYTHON_RUN) ./scripts/generate_ranking_baseline.py

clean:
	rm -rf ./build

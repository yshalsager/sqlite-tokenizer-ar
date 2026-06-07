# Query Fixtures

This directory contains small public fixtures used by the default smoke tests.

Included:

- `docs.smoke.jsonl`, `docs.complex.jsonl`, `docs.snippets.jsonl`
- `inputs.smoke.jsonl`, `inputs.complex.jsonl`, `inputs.snippets.jsonl`
- parser, compile, expansion, ranking, and snippet baseline fixtures

Large parity/full fixtures are intentionally excluded. Keep corpus-derived fixtures in a private validation repo, then run the optional heavy scripts against that private checkout.

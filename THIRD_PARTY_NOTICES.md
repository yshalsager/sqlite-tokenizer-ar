# Third-Party Notices

This project ports behavior from Lucene's Arabic analysis pipeline and includes small reference assets used for verification.

## Apache Lucene

- Project: Apache Lucene
- Reference asset version: 9.9.0
- Latest checked compatible version: 10.4.0
- License: Apache License 2.0
- Source: https://lucene.apache.org/

Files under `tokenizer/assets/lucene_9_9_0/` are reference copies used to verify the ported behavior and preserve source provenance. The C tokenizer code is a port that targets behavioral compatibility with Lucene Arabic analysis.

On 2026-06-07, Lucene `9.9.0` and `10.4.0` were source-diffed for the ArabicAnalyzer pipeline. No behavior changes were found in `ArabicAnalyzer`, Arabic normalization/stem filters, stopwords, `StandardTokenizer`, lowercase, decimal digit folding, or stop filtering. `ArabicNormalizer` and `ArabicStemmer` only changed Java visibility modifiers.

## Arabic Stopwords

The Arabic stopword list distributed by Lucene is credited in the source file as created by Jacques Savoy and distributed under a BSD license. The generated C header `tokenizer/src/arabic_stopwords_99.h` is derived from that list.

## SQLite

The playground build scripts target the official SQLite WASM source tree. Generated SQLite WASM artifacts are not committed by default.

- Project: SQLite
- Source: https://sqlite.org/
- License: public domain / blessing, as documented by SQLite upstream.

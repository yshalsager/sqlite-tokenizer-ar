static void udf_sqlite_tokenizer_ar_analyze_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_analyze_json expects 1 argument", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }
  const char *text = (const char *)sqlite3_value_text(argv[0]);
  int nText = sqlite3_value_bytes(argv[0]);
  if (text == NULL || nText <= 0) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }

  ArabicTokenizer tokenizer;
  memset(&tokenizer, 0, sizeof(tokenizer));
  AnalyzeTermsBuffer terms;
  memset(&terms, 0, sizeof(terms));
  int rc = arabic_tokenize(
      (Fts5Tokenizer *)&tokenizer, &terms, FTS5_TOKENIZE_QUERY, text, nText, analyze_collect_token);
  if (rc != SQLITE_OK || terms.oom) {
    analyze_terms_buffer_free(&terms);
    sqlite3_result_error_nomem(context);
    return;
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  rc = json_builder_append_char(&json, &jsonLen, &jsonCap, '[');
  for (int i = 0; rc == SQLITE_OK && i < terms.count; i++) {
    if (i > 0) {
      rc = json_builder_append_char(&json, &jsonLen, &jsonCap, ',');
      if (rc != SQLITE_OK) {
        break;
      }
    }
    rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, terms.terms[i], terms.termLens[i]);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&json, &jsonLen, &jsonCap, ']');
  }
  analyze_terms_buffer_free(&terms);
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return;
  }
  if (json == NULL) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }
  json[jsonLen] = '\0';
  sqlite3_result_text(context, json, jsonLen, sqlite3_free);
}

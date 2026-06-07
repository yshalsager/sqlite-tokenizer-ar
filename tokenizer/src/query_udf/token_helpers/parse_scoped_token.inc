static void udf_sqlite_tokenizer_ar_parse_scoped_token_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_parse_scoped_token_json expects 1 argument", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  const char *token = (const char *)sqlite3_value_text(argv[0]);
  int nToken = sqlite3_value_bytes(argv[0]);
  if (token == NULL) {
    sqlite3_result_null(context);
    return;
  }

  const char *scope = NULL;
  int nScope = 0;
  const char *raw = token;
  int nRaw = nToken;
  int splitAt = find_unescaped_char_span(token, 0, nToken, ':');
  if (splitAt > 0) {
    int nCandidate = splitAt;
    const char *candidate = token;
    const char *tail = token + splitAt + 1;
    int nTail = nToken - (splitAt + 1);
    if (nTail > 0) {
      if (token_equals_ascii_ci(candidate, nCandidate, "page")) {
        scope = "page";
        nScope = 4;
      } else if (token_equals_ascii_ci(candidate, nCandidate, "title")) {
        scope = "title";
        nScope = 5;
      }
      if (scope != NULL) {
        raw = tail;
        nRaw = nTail;
      }
    }
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "{\"scope\":", 9);
  if (rc == SQLITE_OK) {
    if (scope == NULL) {
      rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "null", 4);
    } else {
      rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, scope, nScope);
    }
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"raw\":", 7);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, raw, nRaw);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&json, &jsonLen, &jsonCap, '}');
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return;
  }
  json[jsonLen] = '\0';
  sqlite3_result_text(context, json, jsonLen, sqlite3_free);
}


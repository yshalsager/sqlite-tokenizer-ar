static void udf_sqlite_tokenizer_ar_parse_fuzzy_token_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_parse_fuzzy_token_json expects 1 argument", -1);
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

  const char *base = NULL;
  int nBase = 0;
  int edits = 0;
  if (!parse_fuzzy_token_span(token, nToken, &base, &nBase, &edits)) {
    sqlite3_result_null(context);
    return;
  }

  char editsBuf[32];
  int nEdits = sqlite3_snprintf((int)sizeof(editsBuf), editsBuf, "%d", edits) ? (int)strlen(editsBuf) : 0;
  if (nEdits <= 0 || nEdits >= (int)sizeof(editsBuf)) {
    sqlite3_result_error_nomem(context);
    return;
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "{\"base\":", 8);
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, base, nBase);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"edits\":", 9);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, editsBuf, nEdits);
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


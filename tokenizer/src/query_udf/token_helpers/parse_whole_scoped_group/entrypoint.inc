static void udf_sqlite_tokenizer_ar_parse_whole_scoped_group_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_parse_whole_scoped_group_json expects 1 argument", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  const char *query = (const char *)sqlite3_value_text(argv[0]);
  int nQuery = sqlite3_value_bytes(argv[0]);
  if (query == NULL) {
    sqlite3_result_null(context);
    return;
  }

  const char *scope = NULL;
  int nScope = 0;
  const char *inner = NULL;
  int nInner = 0;
  const char *boost = NULL;
  int nBoost = 0;
  char scratch[8192];
  if (!parse_whole_scoped_group_parts(
          query,
          nQuery,
          scratch,
          (int)sizeof(scratch),
          &scope,
          &nScope,
          &inner,
          &nInner,
          &boost,
          &nBoost)) {
    sqlite3_result_null(context);
    return;
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = append_whole_scoped_group_json(
      &json,
      &jsonLen,
      &jsonCap,
      scope,
      nScope,
      inner,
      nInner,
      boost,
      nBoost);
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return;
  }
  json[jsonLen] = '\0';
  sqlite3_result_text(context, json, jsonLen, sqlite3_free);
}

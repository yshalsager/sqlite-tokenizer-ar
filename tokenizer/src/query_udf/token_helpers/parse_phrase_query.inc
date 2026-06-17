static void udf_sqlite_tokenizer_ar_parse_phrase_query_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_parse_phrase_query_json expects 1 argument", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  const char *query = (const char *)sqlite3_value_text(argv[0]);
  int nQuery = sqlite3_value_bytes(argv[0]);
  if (query == NULL || nQuery <= 0) {
    sqlite3_result_null(context);
    return;
  }

  int start = 0;
  int end = 0;
  trim_ascii_space_span(query, nQuery, &start, &end);
  if (end <= start) {
    sqlite3_result_null(context);
    return;
  }

  char phrase[8192];
  int nPhrase = 0;
  int idx = 0;
  if (!parse_quoted_text_unescaped(query, start, end, phrase, (int)sizeof(phrase), &nPhrase, &idx)) {
    sqlite3_result_null(context);
    return;
  }

  int slopValue = 0;
  if (idx < end && query[idx] == '~') {
    idx++;
    int slopStart = idx;
    while (idx < end && isdigit((unsigned char)query[idx])) {
      idx++;
    }
    if (idx <= slopStart) {
      sqlite3_result_null(context);
      return;
    }
    int nSlop = idx - slopStart;
    if (!parse_nonnegative_int_span(query + slopStart, nSlop, INT_MAX, &slopValue)) {
      sqlite3_result_null(context);
      return;
    }
  }

  while (idx < end && isspace((unsigned char)query[idx])) {
    idx++;
  }
  if (idx != end) {
    sqlite3_result_null(context);
    return;
  }

  char slopText[32];
  int nSlopText = sqlite3_snprintf((int)sizeof(slopText), slopText, "%d", slopValue) ? (int)strlen(slopText) : 0;
  if (nSlopText <= 0) {
    sqlite3_result_error(context, "failed to format slop value", -1);
    return;
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "{\"phrase\":", 10);
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, phrase, nPhrase);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"slop\":", 8);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, slopText, nSlopText);
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

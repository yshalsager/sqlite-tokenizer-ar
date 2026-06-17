static int parse_single_phrase_clause_scope(
    const char *query,
    int end,
    int *valueStart,
    const char **scopeOut,
    int *nScopeOut) {
  *scopeOut = NULL;
  *nScopeOut = 0;
  *valueStart = 0;
  if (end >= 5 && query[4] == ':' && token_equals_ascii_ci(query, 4, "page")) {
    *scopeOut = "page";
    *nScopeOut = 4;
    *valueStart = 5;
    return 1;
  }
  if (end >= 6 && query[5] == ':' && token_equals_ascii_ci(query, 5, "title")) {
    *scopeOut = "title";
    *nScopeOut = 5;
    *valueStart = 6;
    return 1;
  }
  return 1;
}

static void udf_sqlite_tokenizer_ar_parse_single_phrase_clause_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_parse_single_phrase_clause_json expects 1 argument", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  const char *queryRaw = (const char *)sqlite3_value_text(argv[0]);
  int nQueryRaw = sqlite3_value_bytes(argv[0]);
  if (queryRaw == NULL || nQueryRaw <= 0) {
    sqlite3_result_null(context);
    return;
  }

  int start = 0;
  int end = 0;
  trim_ascii_space_span(queryRaw, nQueryRaw, &start, &end);
  if (end <= start) {
    sqlite3_result_null(context);
    return;
  }
  const char *query = queryRaw + start;
  int nQuery = end - start;

  const char *scope = NULL;
  int nScope = 0;
  int valueStart = 0;
  if (!parse_single_phrase_clause_scope(query, nQuery, &valueStart, &scope, &nScope)) {
    sqlite3_result_null(context);
    return;
  }

  char phrase[8192];
  int nPhrase = 0;
  int idx = 0;
  if (!parse_quoted_text_unescaped(query, valueStart, nQuery, phrase, (int)sizeof(phrase), &nPhrase, &idx)) {
    sqlite3_result_null(context);
    return;
  }

  int slop = 0;
  if (idx < nQuery && query[idx] == '~') {
    idx++;
    int slopStart = idx;
    while (idx < nQuery && isdigit((unsigned char)query[idx])) {
      idx++;
    }
    if (idx <= slopStart) {
      sqlite3_result_null(context);
      return;
    }
    int nSlop = idx - slopStart;
    if (!parse_nonnegative_int_span(query + slopStart, nSlop, INT_MAX, &slop)) {
      sqlite3_result_null(context);
      return;
    }
  }

  char boostBuf[64];
  int nBoost = copy_default_boost_literal(boostBuf, (int)sizeof(boostBuf));
  if (nBoost <= 0) {
    sqlite3_result_error(context, "failed to format default boost", -1);
    return;
  }
  if (idx < nQuery && query[idx] == '^') {
    idx++;
    int boostEnd = 0;
    if (!parse_decimal_number_span(query, idx, nQuery, &boostEnd) || boostEnd <= idx) {
      sqlite3_result_null(context);
      return;
    }
    int nRawBoost = boostEnd - idx;
    int nFmt = copy_boost_literal_span(query + idx, nRawBoost, boostBuf, (int)sizeof(boostBuf), NULL);
    if (nFmt <= 0) {
      sqlite3_result_null(context);
      return;
    }
    nBoost = nFmt;
    idx = boostEnd;
  }

  while (idx < nQuery && isspace((unsigned char)query[idx])) {
    idx++;
  }
  if (idx != nQuery) {
    sqlite3_result_null(context);
    return;
  }

  char slopBuf[32];
  int nSlop = sqlite3_snprintf((int)sizeof(slopBuf), slopBuf, "%d", slop) ? (int)strlen(slopBuf) : 0;
  if (nSlop <= 0) {
    sqlite3_result_error(context, "failed to format slop", -1);
    return;
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "{\"scope\":", (int)strlen("{\"scope\":"));
  if (rc == SQLITE_OK) {
    if (scope == NULL) {
      rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "null", 4);
    } else {
      rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, scope, nScope);
    }
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"phrase\":", (int)strlen(",\"phrase\":"));
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, phrase, nPhrase);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"slop\":", (int)strlen(",\"slop\":"));
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, slopBuf, nSlop);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"boost\":", (int)strlen(",\"boost\":"));
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_boost_literal(&json, &jsonLen, &jsonCap, boostBuf, nBoost);
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

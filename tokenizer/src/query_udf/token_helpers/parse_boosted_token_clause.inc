static int parse_boosted_token_clause_runtime_field(const char *runtimeField, int nRuntimeField) {
  if (runtimeField == NULL || nRuntimeField <= 0) {
    return 0;
  }
  if (token_equals_ascii_ci(runtimeField, nRuntimeField, "page")) {
    return 1;
  }
  if (token_equals_ascii_ci(runtimeField, nRuntimeField, "title")) {
    return 2;
  }
  if (token_equals_ascii_ci(runtimeField, nRuntimeField, "both")) {
    return 3;
  }
  return 0;
}

static int parse_boosted_token_clause_scope(const char *token, int nToken, const char **scopeOut, int *nScopeOut, const char **rawOut, int *nRawOut) {
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

  *scopeOut = scope;
  *nScopeOut = nScope;
  *rawOut = raw;
  *nRawOut = nRaw;
  return 1;
}

static int parse_boosted_token_clause_strip_boost(
    const char *raw,
    int nRaw,
    const char **baseOut,
    int *nBaseOut,
    char *boostBuf,
    int boostBufCap,
    int *nBoostOut,
    double *boostValueOut) {
  int splitAt = -1;
  for (int idx = nRaw - 1; idx >= 0; idx--) {
    if (raw[idx] != '^') {
      continue;
    }
    if (!is_escaped_at(raw, 0, idx)) {
      splitAt = idx;
      break;
    }
  }
  if (splitAt < 0) {
    return 0;
  }

  int boostStart = splitAt + 1;
  int boostEnd = 0;
  if (!parse_decimal_number_span(raw, boostStart, nRaw, &boostEnd) || boostEnd != nRaw) {
    return 0;
  }
  if (splitAt <= 0) {
    return -1;
  }

  int nNum = nRaw - boostStart;
  double boostValue = 0.0;
  int nBoost = copy_boost_literal_span(raw + boostStart, nNum, boostBuf, boostBufCap, &boostValue);
  if (nBoost <= 0) {
    return -1;
  }

  *baseOut = raw;
  *nBaseOut = splitAt;
  *nBoostOut = nBoost;
  *boostValueOut = boostValue;
  return 1;
}

static int parse_boosted_token_clause_has_unescaped_wildcard(const char *base, int nBase) {
  return (find_unescaped_char_span(base, 0, nBase, '*') >= 0) || (find_unescaped_char_span(base, 0, nBase, '?') >= 0);
}

static int parse_boosted_token_clause_is_fuzzy(const char *base, int nBase) {
  const char *fuzzyBase = NULL;
  int nFuzzyBase = 0;
  int edits = 0;
  return parse_fuzzy_token_span(base, nBase, &fuzzyBase, &nFuzzyBase, &edits);
}

static void udf_sqlite_tokenizer_ar_parse_boosted_token_clause_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 2) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_parse_boosted_token_clause_json expects 2 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  const char *token = (const char *)sqlite3_value_text(argv[0]);
  int nToken = sqlite3_value_bytes(argv[0]);
  const char *runtimeField = (const char *)sqlite3_value_text(argv[1]);
  int nRuntimeField = sqlite3_value_bytes(argv[1]);
  if (token == NULL || nToken <= 0 || runtimeField == NULL || nRuntimeField <= 0) {
    sqlite3_result_null(context);
    return;
  }

  int runtimeKind = parse_boosted_token_clause_runtime_field(runtimeField, nRuntimeField);
  if (runtimeKind == 0) {
    sqlite3_result_null(context);
    return;
  }

  const char *scope = NULL;
  int nScope = 0;
  const char *raw = NULL;
  int nRaw = 0;
  parse_boosted_token_clause_scope(token, nToken, &scope, &nScope, &raw, &nRaw);
  if (scope != NULL) {
    int scopeKind = parse_boosted_token_clause_runtime_field(scope, nScope);
    if (runtimeKind != 3 && scopeKind != runtimeKind) {
      sqlite3_result_null(context);
      return;
    }
  }

  const char *base = NULL;
  int nBase = 0;
  char boostBuf[64];
  int nBoost = 0;
  double boostValue = 0.0;
  int stripResult = parse_boosted_token_clause_strip_boost(
      raw, nRaw, &base, &nBase, boostBuf, (int)sizeof(boostBuf), &nBoost, &boostValue);
  if (stripResult == 0) {
    sqlite3_result_null(context);
    return;
  }
  if (stripResult < 0) {
    sqlite3_result_error(context, "invalid boosted token", -1);
    return;
  }
  if (nBase <= 0) {
    sqlite3_result_error(context, "invalid boosted token", -1);
    return;
  }
  if (fabs(boostValue - 1.0) < 1e-12) {
    sqlite3_result_null(context);
    return;
  }

  int hasWildcard = parse_boosted_token_clause_has_unescaped_wildcard(base, nBase);
  int isFuzzy = parse_boosted_token_clause_is_fuzzy(base, nBase);

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
    rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, base, nBase);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"boost\":", 9);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_boost_literal(&json, &jsonLen, &jsonCap, boostBuf, nBoost);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"has_wildcard\":", 16);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, hasWildcard ? "true" : "false", hasWildcard ? 4 : 5);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"is_fuzzy\":", 12);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, isFuzzy ? "true" : "false", isFuzzy ? 4 : 5);
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

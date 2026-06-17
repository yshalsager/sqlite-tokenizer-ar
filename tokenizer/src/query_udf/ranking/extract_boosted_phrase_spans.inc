static int parse_boosted_phrase_span_candidate(
    const char *query,
    int nQuery,
    int start,
    const char **scopeOut,
    int *nScopeOut,
    char *phraseBuf,
    int phraseBufCap,
    int *nPhraseOut,
    int *slopOut,
    char *boostBuf,
    int boostBufCap,
    int *nBoostOut,
    int *nextOut) {
  int idx = start;
  const char *scope = NULL;
  int nScope = 0;
  if ((idx + 5) < nQuery && token_equals_ascii_ci(query + idx, 4, "page") && query[idx + 4] == ':' && query[idx + 5] == '"') {
    scope = "page";
    nScope = 4;
    idx += 5;
  } else if (
      (idx + 6) < nQuery && token_equals_ascii_ci(query + idx, 5, "title") && query[idx + 5] == ':' && query[idx + 6] == '"') {
    scope = "title";
    nScope = 5;
    idx += 6;
  } else {
    if (query[idx] != '"' || is_escaped_at(query, 0, idx)) {
      return 0;
    }
  }

  int nPhrase = 0;
  int afterQuote = 0;
  if (!parse_quoted_text_unescaped(query, idx, nQuery, phraseBuf, phraseBufCap, &nPhrase, &afterQuote)) {
    return 0;
  }
  if (nPhrase <= 0) {
    return 0;
  }

  idx = afterQuote;
  int slop = 0;
  if (idx < nQuery && query[idx] == '~') {
    idx++;
    int slopEnd = 0;
    if (!parse_decimal_number_span(query, idx, nQuery, &slopEnd)) {
      return 0;
    }
    int nSlopRaw = slopEnd - idx;
    if (!parse_nonnegative_int_span(query + idx, nSlopRaw, INT_MAX, &slop)) {
      return 0;
    }
    idx = slopEnd;
  }

  if (idx >= nQuery || query[idx] != '^' || is_escaped_at(query, 0, idx)) {
    return 0;
  }
  idx++;
  int boostEnd = 0;
  if (!parse_decimal_number_span(query, idx, nQuery, &boostEnd) || !is_query_boost_boundary(query, boostEnd, nQuery)) {
    return 0;
  }
  int nBoostRaw = boostEnd - idx;
  double boostValue = 0.0;
  int nBoost = copy_boost_literal_span(query + idx, nBoostRaw, boostBuf, boostBufCap, &boostValue);
  if (nBoost <= 0) {
    return 0;
  }
  double boostDelta = boostValue - 1.0;
  if (boostDelta < 0.0) {
    boostDelta = -boostDelta;
  }
  if (boostDelta < 1e-12) {
    return 0;
  }

  *scopeOut = scope;
  *nScopeOut = nScope;
  *nPhraseOut = nPhrase;
  *slopOut = slop;
  *nBoostOut = nBoost;
  *nextOut = boostEnd;
  return 1;
}

static void udf_sqlite_tokenizer_ar_extract_boosted_phrase_spans_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_extract_boosted_phrase_spans_json expects 1 argument", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }

  const char *query = (const char *)sqlite3_value_text(argv[0]);
  int nQuery = sqlite3_value_bytes(argv[0]);
  if (query == NULL || nQuery <= 0) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_char(&json, &jsonLen, &jsonCap, '[');
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return;
  }

  int needsComma = 0;
  int idx = 0;
  while (idx < nQuery) {
    const char *scope = NULL;
    int nScope = 0;
    char phraseBuf[8192];
    int nPhrase = 0;
    int slop = 0;
    char boostBuf[64];
    int nBoost = 0;
    int nextIdx = idx;

    int parsed = parse_boosted_phrase_span_candidate(
        query,
        nQuery,
        idx,
        &scope,
        &nScope,
        phraseBuf,
        (int)sizeof(phraseBuf),
        &nPhrase,
        &slop,
        boostBuf,
        (int)sizeof(boostBuf),
        &nBoost,
        &nextIdx);
    if (parsed) {
      if (needsComma) {
        rc = json_builder_append_char(&json, &jsonLen, &jsonCap, ',');
      }
      if (rc == SQLITE_OK) {
        rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "{\"scope\":", 9);
      }
      if (rc == SQLITE_OK) {
        if (scope == NULL) {
          rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "null", 4);
        } else {
          rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, scope, nScope);
        }
      }
      if (rc == SQLITE_OK) {
        rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"phrase\":", 10);
      }
      if (rc == SQLITE_OK) {
        rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, phraseBuf, nPhrase);
      }
      if (rc == SQLITE_OK) {
        rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"slop\":", 8);
      }
      if (rc == SQLITE_OK) {
        rc = json_builder_append_int(&json, &jsonLen, &jsonCap, slop);
      }
      if (rc == SQLITE_OK) {
        rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"boost\":", 9);
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
      needsComma = 1;
      idx = nextIdx;
      continue;
    }
    idx++;
  }

  rc = json_builder_append_char(&json, &jsonLen, &jsonCap, ']');
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return;
  }
  json[jsonLen] = '\0';
  sqlite3_result_text(context, json, jsonLen, sqlite3_free);
}

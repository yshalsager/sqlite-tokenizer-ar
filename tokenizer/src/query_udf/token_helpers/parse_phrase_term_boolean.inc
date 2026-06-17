typedef struct ParsedPhraseTermBooleanClause {
  int kind; /* 1=term, 2=phrase */
  const char *scope;
  int nScope;
  const char *term;
  int nTerm;
  char phrase[8192];
  int nPhrase;
  int slop;
  char boostBuf[64];
  int nBoost;
} ParsedPhraseTermBooleanClause;

static int append_json_nullable_slice(
    char **json,
    int *jsonLen,
    int *jsonCap,
    const char *text,
    int nText) {
  if (text == NULL) {
    return json_builder_append_bytes(json, jsonLen, jsonCap, "null", 4);
  }
  return json_builder_append_escaped_string(json, jsonLen, jsonCap, text, nText);
}

static int append_json_int_value(char **json, int *jsonLen, int *jsonCap, int value) {
  char buf[32];
  int nBuf = sqlite3_snprintf((int)sizeof(buf), buf, "%d", value) ? (int)strlen(buf) : 0;
  if (nBuf <= 0) {
    return SQLITE_ERROR;
  }
  return json_builder_append_bytes(json, jsonLen, jsonCap, buf, nBuf);
}

static int parse_phrase_term_boolean_optional_scope(
    const char *query,
    int start,
    int end,
    const char **outScope,
    int *outNScope,
    int *outNext) {
  *outScope = NULL;
  *outNScope = 0;
  *outNext = start;
  if (start + 5 <= end && query[start + 4] == ':' && token_equals_ascii_ci(query + start, 4, "page")) {
    *outScope = "page";
    *outNScope = 4;
    *outNext = start + 5;
    return 1;
  }
  if (start + 6 <= end && query[start + 5] == ':' && token_equals_ascii_ci(query + start, 5, "title")) {
    *outScope = "title";
    *outNScope = 5;
    *outNext = start + 6;
    return 1;
  }
  return 1;
}

static int parse_phrase_term_boolean_phrase_clause(
    const char *query,
    int start,
    int end,
    ParsedPhraseTermBooleanClause *out,
    int *outNext) {
  memset(out, 0, sizeof(*out));
  out->nBoost = copy_default_boost_literal(out->boostBuf, (int)sizeof(out->boostBuf));
  if (out->nBoost <= 0) {
    return 0;
  }

  int idx = start;
  if (!parse_phrase_term_boolean_optional_scope(query, idx, end, &out->scope, &out->nScope, &idx)) {
    return 0;
  }

  int afterQuote = 0;
  if (!parse_quoted_text_unescaped(query, idx, end, out->phrase, (int)sizeof(out->phrase), &out->nPhrase, &afterQuote)) {
    return 0;
  }
  idx = afterQuote;
  out->slop = 0;
  if (idx < end && query[idx] == '~') {
    idx++;
    int slopStart = idx;
    while (idx < end && isdigit((unsigned char)query[idx])) {
      idx++;
    }
    if (idx <= slopStart) {
      return 0;
    }
    int nSlop = idx - slopStart;
    if (!parse_nonnegative_int_span(query + slopStart, nSlop, INT_MAX, &out->slop)) {
      return 0;
    }
  }
  if (idx < end && query[idx] == '^') {
    idx++;
    int boostEnd = 0;
    if (!parse_decimal_number_span(query, idx, end, &boostEnd)) {
      return 0;
    }
    int nRawBoost = boostEnd - idx;
    int nFmt = copy_boost_literal_span(query + idx, nRawBoost, out->boostBuf, (int)sizeof(out->boostBuf), NULL);
    if (nFmt <= 0) {
      return 0;
    }
    out->nBoost = nFmt;
    idx = boostEnd;
  }

  out->kind = 2;
  *outNext = idx;
  return 1;
}

static int parse_phrase_term_boolean_term_clause(
    const char *query,
    int start,
    int end,
    ParsedPhraseTermBooleanClause *out,
    int *outNext) {
  memset(out, 0, sizeof(*out));
  out->nBoost = copy_default_boost_literal(out->boostBuf, (int)sizeof(out->boostBuf));
  if (out->nBoost <= 0) {
    return 0;
  }

  int idx = start;
  if (!parse_phrase_term_boolean_optional_scope(query, idx, end, &out->scope, &out->nScope, &idx)) {
    return 0;
  }

  int tokenStart = idx;
  while (idx < end && !isspace((unsigned char)query[idx])) {
    idx++;
  }
  int tokenEnd = idx;
  if (tokenEnd <= tokenStart) {
    return 0;
  }

  int splitAt = -1;
  for (int at = tokenEnd - 1; at >= tokenStart; at--) {
    if (query[at] != '^') {
      continue;
    }
    if (!is_escaped_at(query, tokenStart, at)) {
      splitAt = at;
      break;
    }
  }

  int baseEnd = tokenEnd;
  if (splitAt > tokenStart) {
    int boostStart = splitAt + 1;
    int boostEnd = 0;
    if (parse_decimal_number_span(query, boostStart, tokenEnd, &boostEnd) && boostEnd == tokenEnd) {
      int nRawBoost = tokenEnd - boostStart;
      int nFmt = copy_boost_literal_span(query + boostStart, nRawBoost, out->boostBuf, (int)sizeof(out->boostBuf), NULL);
      if (nFmt <= 0) {
        return 0;
      }
      out->nBoost = nFmt;
      baseEnd = splitAt;
    }
  }
  if (baseEnd <= tokenStart) {
    return 0;
  }

  out->kind = 1;
  out->term = query + tokenStart;
  out->nTerm = baseEnd - tokenStart;
  out->slop = 0;
  *outNext = idx;
  return 1;
}

static int parse_phrase_term_boolean_clause(
    const char *query,
    int start,
    int end,
    ParsedPhraseTermBooleanClause *out,
    int *outNext) {
  if (parse_phrase_term_boolean_phrase_clause(query, start, end, out, outNext)) {
    return 1;
  }
  if (parse_phrase_term_boolean_term_clause(query, start, end, out, outNext)) {
    return 1;
  }
  return 0;
}

static void udf_sqlite_tokenizer_ar_parse_phrase_term_boolean_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_parse_phrase_term_boolean_json expects 1 argument", -1);
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

  ParsedPhraseTermBooleanClause left;
  ParsedPhraseTermBooleanClause right;
  int idx = start;
  if (!parse_phrase_term_boolean_clause(query, idx, end, &left, &idx)) {
    sqlite3_result_null(context);
    return;
  }

  int leftSpacing = 0;
  while (idx < end && isspace((unsigned char)query[idx])) {
    idx++;
    leftSpacing++;
  }
  if (leftSpacing <= 0) {
    sqlite3_result_null(context);
    return;
  }

  int opStart = idx;
  while (idx < end && !isspace((unsigned char)query[idx])) {
    idx++;
  }
  int nOp = idx - opStart;
  if (nOp <= 0) {
    sqlite3_result_null(context);
    return;
  }
  const char *op = query + opStart;
  if (!(token_equals_ascii_ci(op, nOp, "AND") || token_equals_ascii_ci(op, nOp, "OR"))) {
    sqlite3_result_null(context);
    return;
  }

  int rightSpacing = 0;
  while (idx < end && isspace((unsigned char)query[idx])) {
    idx++;
    rightSpacing++;
  }
  if (rightSpacing <= 0) {
    sqlite3_result_null(context);
    return;
  }

  int rightParsed = 0;
  if (left.kind == 2) {
    if (parse_phrase_term_boolean_phrase_clause(query, idx, end, &right, &idx)) {
      rightParsed = 1;
    } else if (parse_phrase_term_boolean_term_clause(query, idx, end, &right, &idx)) {
      rightParsed = 1;
    }
  } else {
    if (parse_phrase_term_boolean_phrase_clause(query, idx, end, &right, &idx)) {
      rightParsed = 1;
    }
  }
  if (!rightParsed) {
    sqlite3_result_null(context);
    return;
  }

  while (idx < end && isspace((unsigned char)query[idx])) {
    idx++;
  }
  if (idx != end) {
    sqlite3_result_null(context);
    return;
  }
  if (left.kind == 1 && right.kind == 1) {
    sqlite3_result_null(context);
    return;
  }

  const char *shape = NULL;
  int nShape = 0;
  if (left.kind == 2 && right.kind == 2) {
    shape = "phrase_phrase";
    nShape = 13;
  } else if (left.kind == 2 && right.kind == 1) {
    shape = "phrase_term";
    nShape = 11;
  } else if (left.kind == 1 && right.kind == 2) {
    shape = "term_phrase";
    nShape = 11;
  } else {
    sqlite3_result_null(context);
    return;
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "{\"shape\":", (int)strlen("{\"shape\":"));
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, shape, nShape);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"op\":", (int)strlen(",\"op\":"));
  }
  if (rc == SQLITE_OK) {
    if (token_equals_ascii_ci(op, nOp, "AND")) {
      rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "\"AND\"", (int)strlen("\"AND\""));
    } else {
      rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "\"OR\"", (int)strlen("\"OR\""));
    }
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"left_scope\":", (int)strlen(",\"left_scope\":"));
  }
  if (rc == SQLITE_OK) {
    rc = append_json_nullable_slice(&json, &jsonLen, &jsonCap, left.scope, left.nScope);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"left_term\":", (int)strlen(",\"left_term\":"));
  }
  if (rc == SQLITE_OK) {
    rc = append_json_nullable_slice(&json, &jsonLen, &jsonCap, left.kind == 1 ? left.term : NULL, left.nTerm);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"left_phrase\":", (int)strlen(",\"left_phrase\":"));
  }
  if (rc == SQLITE_OK) {
    rc = append_json_nullable_slice(&json, &jsonLen, &jsonCap, left.kind == 2 ? left.phrase : NULL, left.nPhrase);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"left_slop\":", (int)strlen(",\"left_slop\":"));
  }
  if (rc == SQLITE_OK) {
    rc = append_json_int_value(&json, &jsonLen, &jsonCap, left.kind == 2 ? left.slop : 0);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"left_boost\":", (int)strlen(",\"left_boost\":"));
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_boost_literal(&json, &jsonLen, &jsonCap, left.boostBuf, left.nBoost);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"right_scope\":", (int)strlen(",\"right_scope\":"));
  }
  if (rc == SQLITE_OK) {
    rc = append_json_nullable_slice(&json, &jsonLen, &jsonCap, right.scope, right.nScope);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"right_term\":", (int)strlen(",\"right_term\":"));
  }
  if (rc == SQLITE_OK) {
    rc = append_json_nullable_slice(&json, &jsonLen, &jsonCap, right.kind == 1 ? right.term : NULL, right.nTerm);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"right_phrase\":", (int)strlen(",\"right_phrase\":"));
  }
  if (rc == SQLITE_OK) {
    rc = append_json_nullable_slice(&json, &jsonLen, &jsonCap, right.kind == 2 ? right.phrase : NULL, right.nPhrase);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"right_slop\":", (int)strlen(",\"right_slop\":"));
  }
  if (rc == SQLITE_OK) {
    rc = append_json_int_value(&json, &jsonLen, &jsonCap, right.kind == 2 ? right.slop : 0);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"right_boost\":", (int)strlen(",\"right_boost\":"));
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_boost_literal(&json, &jsonLen, &jsonCap, right.boostBuf, right.nBoost);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&json, &jsonLen, &jsonCap, '}');
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    if (rc == SQLITE_NOMEM) {
      sqlite3_result_error_nomem(context);
    } else {
      sqlite3_result_error(context, "failed to build phrase/term boolean JSON", -1);
    }
    return;
  }
  json[jsonLen] = '\0';
  sqlite3_result_text(context, json, jsonLen, sqlite3_free);
}

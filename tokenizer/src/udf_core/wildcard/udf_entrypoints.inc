static void udf_sqlite_tokenizer_ar_wildcard_match(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 2) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_wildcard_match expects 2 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  const char *text = (const char *)sqlite3_value_text(argv[0]);
  int nText = sqlite3_value_bytes(argv[0]);
  const char *pattern = (const char *)sqlite3_value_text(argv[1]);
  int nPattern = sqlite3_value_bytes(argv[1]);
  if (text == NULL || pattern == NULL) {
    sqlite3_result_null(context);
    return;
  }
  if (nPattern <= 0) {
    sqlite3_result_int(context, 1);
    return;
  }
  if (!wildcard_has_meta(pattern, nPattern)) {
    sqlite3_result_int(context, bytes_contains(text, nText, pattern, nPattern));
    return;
  }

  int textCap = 0;
  int patternCap = 0;
  if (ar_checked_int_sum(nText, 1, &textCap) != SQLITE_OK || ar_checked_int_sum(nPattern, 1, &patternCap) != SQLITE_OK) {
    sqlite3_result_error(context, "input too large", -1);
    return;
  }
  uint32_t *textCodepoints = ar_malloc_array(textCap, sizeof(uint32_t));
  uint32_t *patternCodepoints = ar_malloc_array(patternCap, sizeof(uint32_t));
  if (textCodepoints == NULL || patternCodepoints == NULL) {
    sqlite3_free(textCodepoints);
    sqlite3_free(patternCodepoints);
    sqlite3_result_error_nomem(context);
    return;
  }
  int textLen = utf8_to_codepoints(text, nText, textCodepoints, textCap);
  int patternLen = utf8_to_codepoints(pattern, nPattern, patternCodepoints, patternCap);
  int matched = wildcard_search_codepoints(textCodepoints, textLen, patternCodepoints, patternLen);
  sqlite3_free(textCodepoints);
  sqlite3_free(patternCodepoints);
  if (matched < 0) {
    sqlite3_result_error_nomem(context);
    return;
  }
  sqlite3_result_int(context, matched != 0);
}

static void udf_sqlite_tokenizer_ar_levenshtein(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 3) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_levenshtein expects 3 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL ||
      sqlite3_value_type(argv[2]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  const char *left = (const char *)sqlite3_value_text(argv[0]);
  int nLeft = sqlite3_value_bytes(argv[0]);
  const char *right = (const char *)sqlite3_value_text(argv[1]);
  int nRight = sqlite3_value_bytes(argv[1]);
  int maxEdits = sqlite3_value_int(argv[2]);
  if (left == NULL || right == NULL) {
    sqlite3_result_null(context);
    return;
  }
  if (maxEdits < 0) {
    maxEdits = 0;
  }

  int leftCap = 0;
  int rightCap = 0;
  if (ar_checked_int_sum(nLeft, 1, &leftCap) != SQLITE_OK || ar_checked_int_sum(nRight, 1, &rightCap) != SQLITE_OK) {
    sqlite3_result_error(context, "input too large", -1);
    return;
  }
  uint32_t *leftCodepoints = ar_malloc_array(leftCap, sizeof(uint32_t));
  uint32_t *rightCodepoints = ar_malloc_array(rightCap, sizeof(uint32_t));
  if (leftCodepoints == NULL || rightCodepoints == NULL) {
    sqlite3_free(leftCodepoints);
    sqlite3_free(rightCodepoints);
    sqlite3_result_error_nomem(context);
    return;
  }
  int nLeftCodepoints = utf8_to_codepoints(left, nLeft, leftCodepoints, leftCap);
  int nRightCodepoints = utf8_to_codepoints(right, nRight, rightCodepoints, rightCap);
  int distance = levenshtein_codepoints(leftCodepoints, nLeftCodepoints, rightCodepoints, nRightCodepoints, maxEdits);
  sqlite3_free(leftCodepoints);
  sqlite3_free(rightCodepoints);
  if (distance < 0) {
    sqlite3_result_error_nomem(context);
    return;
  }
  sqlite3_result_int(context, distance);
}


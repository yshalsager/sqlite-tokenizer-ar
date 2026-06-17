static void udf_sqlite_tokenizer_ar_stem(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_stem expects 1 argument", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  const char *text = (const char *)sqlite3_value_text(argv[0]);
  int nText = sqlite3_value_bytes(argv[0]);
  if (text == NULL) {
    sqlite3_result_null(context);
    return;
  }

  if (nText == INT_MAX) {
    sqlite3_result_error(context, "input too large", -1);
    return;
  }
  char *stemmed = sqlite3_malloc64((sqlite3_uint64)nText + 1);
  if (stemmed == NULL) {
    sqlite3_result_error_nomem(context);
    return;
  }
  int stemmedLen = stem_token_arabic_light(text, nText, stemmed, nText);
  if (stemmedLen < 0) {
    sqlite3_free(stemmed);
    sqlite3_result_error_nomem(context);
    return;
  }
  stemmed[stemmedLen] = '\0';
  sqlite3_result_text(context, stemmed, stemmedLen, sqlite3_free);
}


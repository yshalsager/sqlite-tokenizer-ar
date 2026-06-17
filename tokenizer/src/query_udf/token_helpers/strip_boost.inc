static void udf_sqlite_tokenizer_ar_strip_boost_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_strip_boost_json expects 1 argument", -1);
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

  int splitAt = -1;
  for (int idx = nToken - 1; idx >= 0; idx--) {
    if (token[idx] != '^') {
      continue;
    }
    if (!is_escaped_at(token, 0, idx)) {
      splitAt = idx;
      break;
    }
  }

  const char *base = token;
  int nBase = nToken;
  int hasBoost = 0;
  char boostBuf[64];
  int nBoost = 0;
  if (splitAt >= 0) {
    int boostStart = splitAt + 1;
    int boostEnd = 0;
    if (parse_decimal_number_span(token, boostStart, nToken, &boostEnd) && boostEnd == nToken) {
      base = token;
      nBase = splitAt;
      if (nBase <= 0) {
        sqlite3_result_error(context, "invalid boosted token", -1);
        return;
      }
      int nNum = nToken - boostStart;
      nBoost = copy_boost_literal_span(token + boostStart, nNum, boostBuf, (int)sizeof(boostBuf), NULL);
      if (nBoost <= 0) {
        sqlite3_result_error(context, "invalid boosted token", -1);
        return;
      }
      hasBoost = 1;
    }
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "{\"base\":", 8);
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, base, nBase);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"boost\":", 9);
  }
  if (rc == SQLITE_OK) {
    if (!hasBoost) {
      rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "null", 4);
    } else {
      rc = json_builder_append_boost_literal(&json, &jsonLen, &jsonCap, boostBuf, nBoost);
    }
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


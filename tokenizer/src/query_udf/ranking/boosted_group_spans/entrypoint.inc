static void udf_sqlite_tokenizer_ar_extract_boosted_group_spans_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 2) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_extract_boosted_group_spans_json expects 2 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }

  const char *queryRaw = (const char *)sqlite3_value_text(argv[0]);
  int nQueryRaw = sqlite3_value_bytes(argv[0]);
  const char *runtimeFieldRaw = (const char *)sqlite3_value_text(argv[1]);
  int nRuntimeFieldRaw = sqlite3_value_bytes(argv[1]);
  if (queryRaw == NULL || runtimeFieldRaw == NULL) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }

  const char *runtimeField = NULL;
  int nRuntimeField = 0;
  if (!parse_runtime_field_for_boosted_group(
          context,
          runtimeFieldRaw,
          nRuntimeFieldRaw,
          &runtimeField,
          &nRuntimeField)) {
    return;
  }

  int start = 0;
  int end = nQueryRaw;
  if (!trim_ascii_space_bounds(queryRaw, nQueryRaw, &start, &end)) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }

  const char *query = queryRaw + start;
  int nQuery = end - start;
  int stackCap = 0;
  int stackCapRc = ar_checked_int_sum(nQuery, 1, &stackCap);
  if (stackCapRc != SQLITE_OK) {
    sqlite3_result_error_toobig(context);
    return;
  }
  int *stack = (int *)ar_malloc_array(stackCap, sizeof(int));
  if (stack == NULL) {
    sqlite3_result_error_nomem(context);
    return;
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_char(&json, &jsonLen, &jsonCap, '[');
  if (rc != SQLITE_OK) {
    sqlite3_free(stack);
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return;
  }

  int needsComma = 0;
  int stackLen = 0;
  int inQuote = 0;
  int idx = 0;
  while (idx < nQuery) {
    char ch = query[idx];
    if (ch == '\\' && idx + 1 < nQuery) {
      idx += 2;
      continue;
    }
    if (ch == '"') {
      inQuote = !inQuote;
      idx++;
      continue;
    }
    if (inQuote) {
      idx++;
      continue;
    }
    if (ch == '(') {
      stack[stackLen++] = idx;
      idx++;
      continue;
    }
    if (ch == ')') {
      int openIdx = -1;
      if (stackLen > 0) {
        openIdx = stack[--stackLen];
      }
      int closeIdx = idx;
      rc = maybe_append_boosted_group_span(
          query,
          nQuery,
          runtimeField,
          nRuntimeField,
          openIdx,
          closeIdx,
          &idx,
          &json,
          &jsonLen,
          &jsonCap,
          &needsComma);
      if (rc != SQLITE_OK) {
        sqlite3_free(stack);
        sqlite3_free(json);
        sqlite3_result_error_nomem(context);
        return;
      }
      continue;
    }
    idx++;
  }

  rc = json_builder_append_char(&json, &jsonLen, &jsonCap, ']');
  sqlite3_free(stack);
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return;
  }
  json[jsonLen] = '\0';
  sqlite3_result_text(context, json, jsonLen, sqlite3_free);
}

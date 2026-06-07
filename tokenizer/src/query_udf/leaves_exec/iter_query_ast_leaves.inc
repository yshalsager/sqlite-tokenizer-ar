static void udf_sqlite_tokenizer_ar_iter_query_ast_leaves_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_iter_query_ast_leaves_json expects 1 argument", -1);
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

  ParsedSimpleAstClause clauseA;
  ParsedSimpleAstClause clauseB;
  int clauseCount = 0;
  if (!parse_simple_query_ast_clauses(query, nQuery, &clauseA, &clauseB, &clauseCount)) {
    sqlite3_result_error(
        context,
        "C AST leaves backend is not available: unsupported query shape for current C parser rollout",
        -1);
    return;
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_char(&json, &jsonLen, &jsonCap, '[');
  int needsComma = 0;
  if (rc == SQLITE_OK && clauseCount >= 1) {
    rc = json_builder_append_leaf_entries_from_clause(
        &json,
        &jsonLen,
        &jsonCap,
        &clauseA,
        NULL,
        0,
        &needsComma,
        0);
  }
  if (rc == SQLITE_OK && clauseCount >= 2) {
    rc = json_builder_append_leaf_entries_from_clause(
        &json,
        &jsonLen,
        &jsonCap,
        &clauseB,
        NULL,
        0,
        &needsComma,
        0);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&json, &jsonLen, &jsonCap, ']');
  }
  if (rc == SQLITE_ERROR) {
    sqlite3_free(json);
    sqlite3_result_error(
        context,
        "C AST leaves backend is not available: unsupported query shape for current C parser rollout",
        -1);
    return;
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return;
  }
  json[jsonLen] = '\0';
  sqlite3_result_text(context, json, jsonLen, sqlite3_free);
}


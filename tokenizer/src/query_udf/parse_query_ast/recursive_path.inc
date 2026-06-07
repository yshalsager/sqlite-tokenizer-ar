static int try_result_recursive_simple_ast_json(sqlite3_context *context, const char *query, int nQuery) {
  ParsedSimpleAstClause clauseA;
  ParsedSimpleAstClause clauseB;
  int clauseCount = 0;
  if (!parse_simple_query_ast_clauses(query, nQuery, &clauseA, &clauseB, &clauseCount)) {
    return 0;
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "{\"clauses\":[", 12);
  if (rc == SQLITE_OK && clauseCount >= 1) {
    rc = json_builder_append_parsed_clause_recursive(
        &json,
        &jsonLen,
        &jsonCap,
        &clauseA,
        0);
  }
  if (rc == SQLITE_OK && clauseCount >= 2) {
    rc = json_builder_append_char(&json, &jsonLen, &jsonCap, ',');
  }
  if (rc == SQLITE_OK && clauseCount >= 2) {
    rc = json_builder_append_parsed_clause_recursive(
        &json,
        &jsonLen,
        &jsonCap,
        &clauseB,
        0);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "]}", 2);
  }
  if (rc == SQLITE_ERROR) {
    sqlite3_free(json);
    return 0;
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return 1;
  }

  json[jsonLen] = '\0';
  sqlite3_result_text(context, json, jsonLen, sqlite3_free);
  return 1;
}

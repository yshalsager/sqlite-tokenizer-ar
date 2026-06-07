static void udf_sqlite_tokenizer_ar_parse_query_ast_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_parse_query_ast_json expects 1 argument", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_text(context, "{\"clauses\":[]}", -1, SQLITE_STATIC);
    return;
  }

  const char *query = (const char *)sqlite3_value_text(argv[0]);
  int nQuery = sqlite3_value_bytes(argv[0]);
  if (query == NULL || nQuery <= 0) {
    sqlite3_result_text(context, "{\"clauses\":[]}", -1, SQLITE_STATIC);
    return;
  }

  /* Partial migration implementation: supports simple token, binary boolean, and single field-group clauses. */
  if (try_result_field_group_clause_ast_json(context, query, nQuery)) {
    return;
  }
  if (try_result_recursive_simple_ast_json(context, query, nQuery)) {
    return;
  }
  const char *malformedError = parse_query_ast_classify_malformed_query(query, nQuery);
  if (malformedError != NULL) {
    sqlite3_result_error(context, malformedError, -1);
    return;
  }
  result_simple_token_or_binary_ast_json(context, query, nQuery);
}

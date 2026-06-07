static int try_result_field_group_clause_ast_json(sqlite3_context *context, const char *query, int nQuery) {
  const char *fieldGroupOccur = NULL;
  int nFieldGroupOccur = 0;
  const char *fieldGroupKind = NULL;
  int nFieldGroupKind = 0;
  const char *fieldGroupValue = NULL;
  int nFieldGroupValue = 0;
  char fieldGroupScratch[8192];
  if (!parse_simple_field_group_clause(
          query,
          nQuery,
          &fieldGroupOccur,
          &nFieldGroupOccur,
          &fieldGroupKind,
          &nFieldGroupKind,
          &fieldGroupValue,
          &nFieldGroupValue,
          fieldGroupScratch,
          (int)sizeof(fieldGroupScratch))) {
    return 0;
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "{\"clauses\":[", 12);
  if (rc == SQLITE_OK) {
    rc = json_builder_append_clause_node(
        &json,
        &jsonLen,
        &jsonCap,
        fieldGroupOccur,
        nFieldGroupOccur,
        fieldGroupKind,
        nFieldGroupKind,
        fieldGroupValue,
        nFieldGroupValue);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "]}", 2);
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

static int json_builder_append_parsed_clause_recursive(
    char **json,
    int *jsonLen,
    int *jsonCap,
    const ParsedSimpleAstClause *clause,
    int depth) {
  if (depth > 16) {
    return SQLITE_ERROR;
  }
  if (clause->nKind == 5 && token_equals_ascii_ci(clause->kind, clause->nKind, "group")) {
    ParsedSimpleAstClause innerA;
    ParsedSimpleAstClause innerB;
    int innerCount = 0;
    if (!parse_simple_query_ast_clauses(clause->value, clause->nValue, &innerA, &innerB, &innerCount) || innerCount <= 0) {
      return SQLITE_ERROR;
    }

    int rc = json_builder_append_bytes(json, jsonLen, jsonCap, "{\"occur\":\"", 10);
    if (rc != SQLITE_OK) {
      return rc;
    }
    rc = json_builder_append_bytes(json, jsonLen, jsonCap, clause->occur, clause->nOccur);
    if (rc != SQLITE_OK) {
      return rc;
    }
    rc = json_builder_append_bytes(json, jsonLen, jsonCap, "\",\"node\":{\"kind\":\"group\",\"clauses\":[", 36);
    if (rc != SQLITE_OK) {
      return rc;
    }
    rc = json_builder_append_parsed_clause_recursive(json, jsonLen, jsonCap, &innerA, depth + 1);
    if (rc != SQLITE_OK) {
      return rc;
    }
    if (innerCount >= 2) {
      rc = json_builder_append_char(json, jsonLen, jsonCap, ',');
      if (rc != SQLITE_OK) {
        return rc;
      }
      rc = json_builder_append_parsed_clause_recursive(json, jsonLen, jsonCap, &innerB, depth + 1);
      if (rc != SQLITE_OK) {
        return rc;
      }
    }
    return json_builder_append_bytes(json, jsonLen, jsonCap, "]}}", 3);
  }

  return json_builder_append_clause_node(
      json,
      jsonLen,
      jsonCap,
      clause->occur,
      clause->nOccur,
      clause->kind,
      clause->nKind,
      clause->value,
      clause->nValue);
}

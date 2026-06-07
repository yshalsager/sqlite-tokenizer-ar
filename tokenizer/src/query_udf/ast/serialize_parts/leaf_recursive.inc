static int json_builder_append_leaf_entries_from_clause(
    char **json,
    int *jsonLen,
    int *jsonCap,
    const ParsedSimpleAstClause *clause,
    const char *parentOccur,
    int nParentOccur,
    int *ioNeedsComma,
    int depth) {
  if (depth > 16) {
    return SQLITE_ERROR;
  }
  const char *effectiveOccur = clause->occur;
  int nEffectiveOccur = clause->nOccur;
  compose_clause_occur_text(
      parentOccur,
      nParentOccur,
      clause->occur,
      clause->nOccur,
      &effectiveOccur,
      &nEffectiveOccur);

  if (clause->nKind == 5 && token_equals_ascii_ci(clause->kind, clause->nKind, "group")) {
    ParsedSimpleAstClause innerA;
    ParsedSimpleAstClause innerB;
    int innerCount = 0;
    if (!parse_simple_query_ast_clauses(clause->value, clause->nValue, &innerA, &innerB, &innerCount) || innerCount <= 0) {
      return SQLITE_ERROR;
    }
    int rc = json_builder_append_leaf_entries_from_clause(
        json,
        jsonLen,
        jsonCap,
        &innerA,
        effectiveOccur,
        nEffectiveOccur,
        ioNeedsComma,
        depth + 1);
    if (rc != SQLITE_OK) {
      return rc;
    }
    if (innerCount >= 2) {
      rc = json_builder_append_leaf_entries_from_clause(
          json,
          jsonLen,
          jsonCap,
          &innerB,
          effectiveOccur,
          nEffectiveOccur,
          ioNeedsComma,
          depth + 1);
      if (rc != SQLITE_OK) {
        return rc;
      }
    }
    return SQLITE_OK;
  }

  return json_builder_append_leaf_entry(
      json,
      jsonLen,
      jsonCap,
      effectiveOccur,
      nEffectiveOccur,
      clause->kind,
      clause->nKind,
      clause->value,
      clause->nValue,
      ioNeedsComma);
}

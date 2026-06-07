static int parse_simple_field_group_clause(
    const char *query,
    int nQuery,
    const char **occur,
    int *nOccur,
    const char **kind,
    int *nKind,
    const char **valueStart,
    int *nValue,
    char *scratch,
    int scratchCap) {
  int innerStart = 0;
  const char *scope = NULL;
  int scopeLen = 0;
  const char *occurText = NULL;
  int nOccurText = 0;
  if (!parse_field_group_prefix_and_scope(
          query,
          nQuery,
          &innerStart,
          &scope,
          &scopeLen,
          &occurText,
          &nOccurText)) {
    return 0;
  }

  int closeIdx = 0;
  int afterClose = 0;
  if (!parse_field_group_close_index(query, nQuery, innerStart, &closeIdx, &afterClose)) {
    return 0;
  }
  int innerLen = closeIdx - innerStart;
  if (innerLen < 0) {
    return 0;
  }

  const char *nodeKind = NULL;
  int nNodeKind = 0;
  char boostBuf[64];
  int boostLen = 0;
  if (!parse_field_group_optional_boost(
          query,
          nQuery,
          afterClose,
          &nodeKind,
          &nNodeKind,
          boostBuf,
          (int)sizeof(boostBuf),
          &boostLen)) {
    return 0;
  }

  if (!write_field_group_value_to_scratch(
          query,
          scope,
          scopeLen,
          innerStart,
          innerLen,
          boostBuf,
          boostLen,
          scratch,
          scratchCap,
          valueStart,
          nValue)) {
    return 0;
  }

  *occur = occurText;
  *nOccur = nOccurText;
  *kind = nodeKind;
  *nKind = nNodeKind;
  return 1;
}

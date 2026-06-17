static int maybe_append_boosted_group_span(
    const char *query,
    int nQuery,
    const char *runtimeField,
    int nRuntimeField,
    int openIdx,
    int closeIdx,
    int *ioIdx,
    char **json,
    int *jsonLen,
    int *jsonCap,
    int *ioNeedsComma) {
  int idx = *ioIdx + 1;
  if (idx >= nQuery || query[idx] != '^') {
    *ioIdx = idx;
    return SQLITE_OK;
  }
  idx++;
  int boostStart = idx;
  while (idx < nQuery && (isdigit((unsigned char)query[idx]) || query[idx] == '.')) {
    idx++;
  }
  int nBoostRaw = idx - boostStart;
  if (nBoostRaw <= 0 || openIdx < 0 || !is_query_boost_boundary(query, idx, nQuery)) {
    *ioIdx = idx;
    return SQLITE_OK;
  }

  double boostValue = 0.0;
  if (!parse_boost_literal_double_span(query + boostStart, nBoostRaw, &boostValue)) {
    *ioIdx = idx;
    return SQLITE_OK;
  }
  double boostDelta = boostValue - 1.0;
  if (boostDelta < 0.0) {
    boostDelta = -boostDelta;
  }
  if (boostDelta < 1e-12) {
    *ioIdx = idx;
    return SQLITE_OK;
  }
  const char *scope = NULL;
  int nScope = 0;
  int scopeStart = openIdx;
  if (openIdx >= 6 && token_equals_ascii_ci(query + openIdx - 6, 6, "title:")) {
    scope = "title";
    nScope = 5;
    scopeStart = openIdx - 6;
  } else if (openIdx >= 5 && token_equals_ascii_ci(query + openIdx - 5, 5, "page:")) {
    scope = "page";
    nScope = 4;
    scopeStart = openIdx - 5;
  }

  if (scope != NULL) {
    if (!(nScope == nRuntimeField && memcmp(scope, runtimeField, (size_t)nRuntimeField) == 0)) {
      *ioIdx = idx;
      return SQLITE_OK;
    }
  }
  if (scopeStart == 0 && idx == nQuery) {
    *ioIdx = idx;
    return SQLITE_OK;
  }

  int innerStart = openIdx + 1;
  int innerEnd = closeIdx;
  while (innerStart < innerEnd && isspace((unsigned char)query[innerStart])) {
    innerStart++;
  }
  while (innerEnd > innerStart && isspace((unsigned char)query[innerEnd - 1])) {
    innerEnd--;
  }
  if (innerEnd <= innerStart) {
    *ioIdx = idx;
    return SQLITE_OK;
  }

  int rc = SQLITE_OK;
  if (*ioNeedsComma) {
    rc = json_builder_append_char(json, jsonLen, jsonCap, ',');
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(json, jsonLen, jsonCap, "{\"inner\":", 9);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(
        json,
        jsonLen,
        jsonCap,
        query + innerStart,
        innerEnd - innerStart);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(json, jsonLen, jsonCap, ",\"boost\":", 9);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_boost_literal(json, jsonLen, jsonCap, query + boostStart, nBoostRaw);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(json, jsonLen, jsonCap, '}');
  }
  if (rc == SQLITE_OK) {
    *ioNeedsComma = 1;
  }
  *ioIdx = idx;
  return rc;
}

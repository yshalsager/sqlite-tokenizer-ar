static int json_builder_append_clause_node(
    char **json,
    int *jsonLen,
    int *jsonCap,
    const char *occur,
    int nOccur,
    const char *kind,
    int nKind,
    const char *value,
    int nValue) {
  int rc = json_builder_append_bytes(json, jsonLen, jsonCap, "{\"occur\":\"", 10);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = json_builder_append_bytes(json, jsonLen, jsonCap, occur, nOccur);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = json_builder_append_bytes(json, jsonLen, jsonCap, "\",\"node\":{\"kind\":\"", 18);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = json_builder_append_bytes(json, jsonLen, jsonCap, kind, nKind);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = json_builder_append_bytes(json, jsonLen, jsonCap, "\",\"value\":", 10);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = json_builder_append_escaped_string(json, jsonLen, jsonCap, value, nValue);
  if (rc != SQLITE_OK) {
    return rc;
  }
  return json_builder_append_bytes(json, jsonLen, jsonCap, "}}", 2);
}

static int json_builder_append_clause_token(
    char **json,
    int *jsonLen,
    int *jsonCap,
    const char *occur,
    const char *token,
    int nToken) {
  return json_builder_append_clause_node(json, jsonLen, jsonCap, occur, (int)strlen(occur), "token", 5, token, nToken);
}

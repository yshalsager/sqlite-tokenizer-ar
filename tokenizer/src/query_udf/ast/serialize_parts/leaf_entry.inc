static int json_builder_append_leaf_entry(
    char **json,
    int *jsonLen,
    int *jsonCap,
    const char *occur,
    int nOccur,
    const char *kind,
    int nKind,
    const char *value,
    int nValue,
    int *ioNeedsComma) {
  int rc = SQLITE_OK;
  if (*ioNeedsComma) {
    rc = json_builder_append_char(json, jsonLen, jsonCap, ',');
    if (rc != SQLITE_OK) {
      return rc;
    }
  }
  rc = json_builder_append_bytes(json, jsonLen, jsonCap, "{\"occur\":\"", 10);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = json_builder_append_bytes(json, jsonLen, jsonCap, occur, nOccur);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = json_builder_append_bytes(json, jsonLen, jsonCap, "\",\"kind\":\"", 10);
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
  rc = json_builder_append_char(json, jsonLen, jsonCap, '}');
  if (rc != SQLITE_OK) {
    return rc;
  }
  *ioNeedsComma = 1;
  return SQLITE_OK;
}

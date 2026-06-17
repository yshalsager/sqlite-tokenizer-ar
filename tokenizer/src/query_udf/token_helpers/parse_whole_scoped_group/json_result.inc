static int append_whole_scoped_group_json(
    char **json,
    int *jsonLen,
    int *jsonCap,
    const char *scope,
    int nScope,
    const char *inner,
    int nInner,
    const char *boost,
    int nBoost) {
  int rc = json_builder_append_bytes(json, jsonLen, jsonCap, "{\"scope\":", 9);
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(json, jsonLen, jsonCap, scope, nScope);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(json, jsonLen, jsonCap, ",\"inner\":", 9);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(json, jsonLen, jsonCap, inner, nInner);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(json, jsonLen, jsonCap, ",\"boost\":", 9);
  }
  if (rc == SQLITE_OK) {
    if (boost == NULL) {
      rc = json_builder_append_bytes(json, jsonLen, jsonCap, "null", 4);
    } else {
      rc = json_builder_append_boost_literal(json, jsonLen, jsonCap, boost, nBoost);
    }
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(json, jsonLen, jsonCap, '}');
  }
  return rc;
}

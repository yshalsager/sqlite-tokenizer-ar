static int append_rank_boost_json_result(
    char **json,
    int *jsonLen,
    int *jsonCap,
    const char *boostFactor,
    int nBoostFactor,
    const char *rankingQuery,
    int nRankingQuery) {
  int rc = json_builder_append_bytes(json, jsonLen, jsonCap, "{\"boost_factor\":", 16);
  if (rc == SQLITE_OK) {
    rc = json_builder_append_boost_literal(json, jsonLen, jsonCap, boostFactor, nBoostFactor);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(json, jsonLen, jsonCap, ",\"ranking_query\":", 17);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(json, jsonLen, jsonCap, rankingQuery, nRankingQuery);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(json, jsonLen, jsonCap, '}');
  }
  return rc;
}

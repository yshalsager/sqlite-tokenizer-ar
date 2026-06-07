static void udf_sqlite_tokenizer_ar_preprocess_rank_boost_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 2) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_preprocess_rank_boost_json expects 2 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }

  const char *query = (const char *)sqlite3_value_text(argv[0]);
  int nQuery = sqlite3_value_bytes(argv[0]);
  const char *runtimeFieldRaw = (const char *)sqlite3_value_text(argv[1]);
  int nRuntimeFieldRaw = sqlite3_value_bytes(argv[1]);
  if (query == NULL || runtimeFieldRaw == NULL) {
    sqlite3_result_null(context);
    return;
  }

  const char *runtimeField = NULL;
  int nRuntimeField = 0;
  if (!parse_runtime_field_for_rank_boost(runtimeFieldRaw, nRuntimeFieldRaw, &runtimeField, &nRuntimeField)) {
    sqlite3_result_null(context);
    return;
  }

  const char *rankingQuery = NULL;
  int nRankingQuery = 0;
  char boostFactorBuf[64];
  int nBoostFactor = 0;
  char fieldGroupScratch[8192];

  int fieldGroupRc = try_extract_rank_boost_whole_field_group(
      query,
      nQuery,
      runtimeField,
      nRuntimeField,
      fieldGroupScratch,
      (int)sizeof(fieldGroupScratch),
      &rankingQuery,
      &nRankingQuery,
      boostFactorBuf,
      (int)sizeof(boostFactorBuf),
      &nBoostFactor);
  if (fieldGroupRc < 0) {
    sqlite3_result_null(context);
    return;
  }

  if (rankingQuery == NULL || nRankingQuery <= 0 || nBoostFactor <= 0) {
    int topLevelRc = try_extract_rank_boost_top_level_group(
        query,
        nQuery,
        runtimeField,
        nRuntimeField,
        fieldGroupScratch,
        (int)sizeof(fieldGroupScratch),
        &rankingQuery,
        &nRankingQuery,
        boostFactorBuf,
        (int)sizeof(boostFactorBuf),
        &nBoostFactor);
    if (topLevelRc < 0) {
      sqlite3_result_null(context);
      return;
    }
  }

  if (rankingQuery == NULL || nRankingQuery <= 0 || nBoostFactor <= 0) {
    int leafRc = try_extract_rank_boost_leaf_clause(
        query,
        nQuery,
        runtimeField,
        nRuntimeField,
        &rankingQuery,
        &nRankingQuery,
        boostFactorBuf,
        (int)sizeof(boostFactorBuf),
        &nBoostFactor);
    if (leafRc < 0) {
      sqlite3_result_null(context);
      return;
    }
  }

  if (rankingQuery == NULL || nRankingQuery <= 0 || nBoostFactor <= 0) {
    sqlite3_result_null(context);
    return;
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = append_rank_boost_json_result(
      &json,
      &jsonLen,
      &jsonCap,
      boostFactorBuf,
      nBoostFactor,
      rankingQuery,
      nRankingQuery);
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return;
  }
  json[jsonLen] = '\0';
  sqlite3_result_text(context, json, jsonLen, sqlite3_free);
}

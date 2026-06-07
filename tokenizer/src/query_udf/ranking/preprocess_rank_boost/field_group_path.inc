static int try_extract_rank_boost_whole_field_group(
    const char *query,
    int nQuery,
    const char *runtimeField,
    int nRuntimeField,
    char *fieldGroupScratch,
    int nFieldGroupScratch,
    const char **rankingQueryOut,
    int *nRankingQueryOut,
    char *boostFactorBuf,
    int nBoostFactorBuf,
    int *nBoostFactorOut) {
  const char *occur = NULL;
  int nOccur = 0;
  const char *kind = NULL;
  int nKind = 0;
  const char *value = NULL;
  int nValue = 0;
  if (!(parse_simple_field_group_clause(
            query,
            nQuery,
            &occur,
            &nOccur,
            &kind,
            &nKind,
            &value,
            &nValue,
            fieldGroupScratch,
            nFieldGroupScratch) &&
        nOccur == 6 && memcmp(occur, "SHOULD", 6) == 0)) {
    return 0;
  }

  const char *scope = NULL;
  int nScope = 0;
  const char *inner = NULL;
  int nInner = 0;
  const char *boost = NULL;
  int nBoost = 0;
  if (!parse_field_group_scoped_inner_parts(
          kind,
          nKind,
          value,
          nValue,
          &scope,
          &nScope,
          &inner,
          &nInner,
          &boost,
          &nBoost)) {
    return 0;
  }

  int scopeMatches = (nScope == nRuntimeField) && (memcmp(scope, runtimeField, (size_t)nRuntimeField) == 0);
  if (!scopeMatches) {
    *rankingQueryOut = "__nohit__";
    *nRankingQueryOut = 9;
    *nBoostFactorOut = format_float_like_python(1.0, boostFactorBuf, nBoostFactorBuf);
    return (*nBoostFactorOut > 0) ? 1 : -1;
  }

  *rankingQueryOut = inner;
  *nRankingQueryOut = nInner;
  if (boost != NULL && nBoost > 0) {
    if (nBoost >= nBoostFactorBuf) {
      return -1;
    }
    memcpy(boostFactorBuf, boost, (size_t)nBoost);
    boostFactorBuf[nBoost] = '\0';
    *nBoostFactorOut = nBoost;
  } else {
    *nBoostFactorOut = format_float_like_python(1.0, boostFactorBuf, nBoostFactorBuf);
  }
  return (*nBoostFactorOut > 0) ? 1 : -1;
}

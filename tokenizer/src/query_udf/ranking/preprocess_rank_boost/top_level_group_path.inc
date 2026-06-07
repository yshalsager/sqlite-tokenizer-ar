static int try_extract_rank_boost_top_level_group(
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
  const char *rankingQuery = NULL;
  int nRankingQuery = 0;
  char boostBuf[64];
  int nBoost = 0;
  if (!parse_top_level_group_boost_span(query, nQuery, &rankingQuery, &nRankingQuery, boostBuf, (int)sizeof(boostBuf), &nBoost)) {
    return 0;
  }
  if (nBoost >= nBoostFactorBuf) {
    return -1;
  }
  memcpy(boostFactorBuf, boostBuf, (size_t)nBoost);
  boostFactorBuf[nBoost] = '\0';
  *nBoostFactorOut = nBoost;

  for (int depth = 0; depth < 8; depth++) {
    const char *nestedInner = NULL;
    int nNestedInner = 0;
    char nestedBoostBuf[64];
    int nNestedBoost = 0;
    if (!parse_top_level_group_boost_span(
            rankingQuery,
            nRankingQuery,
            &nestedInner,
            &nNestedInner,
            nestedBoostBuf,
            (int)sizeof(nestedBoostBuf),
            &nNestedBoost)) {
      break;
    }
    rankingQuery = nestedInner;
    nRankingQuery = nNestedInner;
  }

  const char *innerOccur = NULL;
  int nInnerOccur = 0;
  const char *innerKind = NULL;
  int nInnerKind = 0;
  const char *innerValue = NULL;
  int nInnerValue = 0;
  if (parse_simple_field_group_clause(
          rankingQuery,
          nRankingQuery,
          &innerOccur,
          &nInnerOccur,
          &innerKind,
          &nInnerKind,
          &innerValue,
          &nInnerValue,
          fieldGroupScratch,
          nFieldGroupScratch) &&
      nInnerOccur == 6 && memcmp(innerOccur, "SHOULD", 6) == 0) {
    const char *scope = NULL;
    int nScope = 0;
    const char *scopedInner = NULL;
    int nScopedInner = 0;
    const char *boost = NULL;
    int nBoostInner = 0;
    if (parse_field_group_scoped_inner_parts(
            innerKind,
            nInnerKind,
            innerValue,
            nInnerValue,
            &scope,
            &nScope,
            &scopedInner,
            &nScopedInner,
            &boost,
            &nBoostInner)) {
      int scopeMatches = (nScope == nRuntimeField) && (memcmp(scope, runtimeField, (size_t)nRuntimeField) == 0);
      if (!scopeMatches) {
        rankingQuery = "__nohit__";
        nRankingQuery = 9;
      } else {
        rankingQuery = scopedInner;
        nRankingQuery = nScopedInner;
      }
    }
  }

  *rankingQueryOut = rankingQuery;
  *nRankingQueryOut = nRankingQuery;
  return 1;
}

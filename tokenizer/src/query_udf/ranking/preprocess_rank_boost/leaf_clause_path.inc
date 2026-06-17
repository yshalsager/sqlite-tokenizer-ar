static int is_rank_leaf_term_char(unsigned char ch) {
  if (isspace((int)ch)) {
    return 0;
  }
  if (ch == '"' || ch == '(' || ch == ')' || ch == ':' || ch == '+' || ch == '-') {
    return 0;
  }
  return 1;
}

static int parse_rank_leaf_scope(const char *query, int nQuery, int *valueStart, const char **scopeOut, int *nScopeOut) {
  *scopeOut = NULL;
  *nScopeOut = 0;
  *valueStart = 0;
  if (nQuery >= 5 && query[4] == ':' && token_equals_ascii_ci(query, 4, "page")) {
    *scopeOut = "page";
    *nScopeOut = 4;
    *valueStart = 5;
    return 1;
  }
  if (nQuery >= 6 && query[5] == ':' && token_equals_ascii_ci(query, 5, "title")) {
    *scopeOut = "title";
    *nScopeOut = 5;
    *valueStart = 6;
    return 1;
  }
  return 1;
}

static int validate_rank_leaf_phrase_span(const char *text, int start, int endExclusive) {
  if (start >= endExclusive || text[start] != '"') {
    return 0;
  }
  char phraseBuf[8192];
  int nPhrase = 0;
  int afterQuote = 0;
  if (!parse_quoted_text_unescaped(text, start, endExclusive, phraseBuf, (int)sizeof(phraseBuf), &nPhrase, &afterQuote)) {
    return 0;
  }
  int idx = afterQuote;
  if (idx < endExclusive && text[idx] == '~') {
    idx++;
    int slopStart = idx;
    while (idx < endExclusive && isdigit((unsigned char)text[idx])) {
      idx++;
    }
    if (idx <= slopStart) {
      return 0;
    }
  }
  return (idx == endExclusive) ? 1 : 0;
}

static int validate_rank_leaf_term_span(const char *text, int start, int endExclusive) {
  if (start >= endExclusive) {
    return 0;
  }
  for (int i = start; i < endExclusive; i++) {
    if (!is_rank_leaf_term_char((unsigned char)text[i])) {
      return 0;
    }
  }
  return 1;
}

static int try_extract_rank_boost_leaf_clause(
    const char *query,
    int nQuery,
    const char *runtimeField,
    int nRuntimeField,
    const char **rankingQueryOut,
    int *nRankingQueryOut,
    char *boostFactorBuf,
    int nBoostFactorBuf,
    int *nBoostFactorOut) {
  int splitAt = -1;
  for (int i = nQuery - 1; i >= 0; i--) {
    if (query[i] != '^') {
      continue;
    }
    if (!is_escaped_at(query, 0, i)) {
      splitAt = i;
      break;
    }
  }
  if (splitAt <= 0) {
    return 0;
  }

  int boostStart = splitAt + 1;
  int boostEnd = 0;
  if (!parse_decimal_number_span(query, boostStart, nQuery, &boostEnd) || boostEnd != nQuery) {
    return 0;
  }

  int nRawBoost = nQuery - boostStart;
  int nFmtBoost = copy_boost_literal_span(query + boostStart, nRawBoost, boostFactorBuf, nBoostFactorBuf, NULL);
  if (nFmtBoost <= 0) {
    return -1;
  }

  int nBase = splitAt;
  const char *scope = NULL;
  int nScope = 0;
  int valueStart = 0;
  if (!parse_rank_leaf_scope(query, nBase, &valueStart, &scope, &nScope)) {
    return 0;
  }
  if (valueStart >= nBase) {
    return 0;
  }

  int isPhrase = 0;
  if (query[valueStart] == '"') {
    if (!validate_rank_leaf_phrase_span(query, valueStart, nBase)) {
      return 0;
    }
    isPhrase = 1;
  } else {
    if (!validate_rank_leaf_term_span(query, valueStart, nBase)) {
      return 0;
    }
  }
  (void)isPhrase;

  if (scope != NULL) {
    int scopeMatches = (nScope == nRuntimeField) && (memcmp(scope, runtimeField, (size_t)nRuntimeField) == 0);
    if (!scopeMatches) {
      *rankingQueryOut = query;
      *nRankingQueryOut = nQuery;
      *nBoostFactorOut = copy_default_boost_literal(boostFactorBuf, nBoostFactorBuf);
      return (*nBoostFactorOut > 0) ? 1 : -1;
    }
  }

  *rankingQueryOut = query;
  *nRankingQueryOut = nBase;
  *nBoostFactorOut = nFmtBoost;
  return 1;
}

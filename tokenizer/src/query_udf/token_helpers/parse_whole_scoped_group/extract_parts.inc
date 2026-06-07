static int parse_whole_scoped_group_parts(
    const char *query,
    int nQuery,
    char *scratch,
    int nScratch,
    const char **scopeOut,
    int *nScopeOut,
    const char **innerOut,
    int *nInnerOut,
    const char **boostOut,
    int *nBoostOut) {
  const char *occur = NULL;
  int nOccur = 0;
  const char *kind = NULL;
  int nKind = 0;
  const char *value = NULL;
  int nValue = 0;
  if (!parse_simple_field_group_clause(
          query,
          nQuery,
          &occur,
          &nOccur,
          &kind,
          &nKind,
          &value,
          &nValue,
          scratch,
          nScratch)) {
    return 0;
  }
  if (!(nOccur == 6 && memcmp(occur, "SHOULD", 6) == 0)) {
    return 0;
  }

  int firstTab = -1;
  int secondTab = -1;
  for (int i = 0; i < nValue; i++) {
    if (value[i] != '\t') {
      continue;
    }
    if (firstTab < 0) {
      firstTab = i;
    } else {
      secondTab = i;
      break;
    }
  }
  if (firstTab <= 0 || firstTab >= nValue - 1) {
    return 0;
  }

  const char *scope = value;
  int nScope = firstTab;
  const char *inner = NULL;
  int nInner = 0;
  const char *boost = NULL;
  int nBoost = 0;

  if (nKind == 11 && memcmp(kind, "field_group", 11) == 0) {
    inner = value + firstTab + 1;
    nInner = nValue - (firstTab + 1);
  } else if (nKind == 17 && memcmp(kind, "field_group_boost", 17) == 0) {
    if (secondTab <= firstTab + 1 || secondTab >= nValue - 1) {
      return 0;
    }
    boost = value + firstTab + 1;
    nBoost = secondTab - (firstTab + 1);
    inner = value + secondTab + 1;
    nInner = nValue - (secondTab + 1);
  } else {
    return 0;
  }
  if (nInner <= 0) {
    return 0;
  }

  *scopeOut = scope;
  *nScopeOut = nScope;
  *innerOut = inner;
  *nInnerOut = nInner;
  *boostOut = boost;
  *nBoostOut = nBoost;
  return 1;
}

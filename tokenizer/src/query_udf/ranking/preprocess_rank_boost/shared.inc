static int parse_runtime_field_for_rank_boost(
    const char *runtimeFieldRaw,
    int nRuntimeFieldRaw,
    const char **runtimeFieldOut,
    int *nRuntimeFieldOut) {
  if (token_equals_ascii_ci(runtimeFieldRaw, nRuntimeFieldRaw, "page")) {
    *runtimeFieldOut = "page";
    *nRuntimeFieldOut = 4;
    return 1;
  }
  if (token_equals_ascii_ci(runtimeFieldRaw, nRuntimeFieldRaw, "title")) {
    *runtimeFieldOut = "title";
    *nRuntimeFieldOut = 5;
    return 1;
  }
  return 0;
}

static int parse_field_group_scoped_inner_parts(
    const char *kind,
    int nKind,
    const char *value,
    int nValue,
    const char **scopeOut,
    int *nScopeOut,
    const char **innerOut,
    int *nInnerOut,
    const char **boostOut,
    int *nBoostOut) {
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
  if (!(firstTab > 0 && firstTab < nValue - 1)) {
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
    if (secondTab > firstTab + 1 && secondTab < nValue - 1) {
      boost = value + firstTab + 1;
      nBoost = secondTab - (firstTab + 1);
      inner = value + secondTab + 1;
      nInner = nValue - (secondTab + 1);
    }
  }
  if (inner == NULL || nInner <= 0) {
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

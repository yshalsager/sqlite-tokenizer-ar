static int parse_simple_operand_prefix(
    const char *text,
    int start,
    int end,
    ParsedSimpleAstClause *out,
    int *sOut,
    int *eOut,
    const char **scopeStartOut,
    int *nScopeOut) {
  int s = start;
  int e = end;
  while (s < e && isspace((unsigned char)text[s])) {
    s++;
  }
  while (e > s && isspace((unsigned char)text[e - 1])) {
    e--;
  }
  if (s >= e) {
    return 0;
  }

  out->occur = "SHOULD";
  out->nOccur = 6;
  if (text[s] == '+' || text[s] == '-') {
    if (text[s] == '+') {
      out->occur = "MUST";
      out->nOccur = 4;
    } else {
      out->occur = "MUST_NOT";
      out->nOccur = 8;
    }
    s++;
    while (s < e && isspace((unsigned char)text[s])) {
      s++;
    }
    if (s >= e) {
      return 0;
    }
  }
  if ((e - s) >= 3 && token_equals_ascii_ci(text + s, 3, "NOT") && ((s + 3) == e || isspace((unsigned char)text[s + 3]))) {
    out->occur = "MUST_NOT";
    out->nOccur = 8;
    s += 3;
    while (s < e && isspace((unsigned char)text[s])) {
      s++;
    }
    if (s >= e) {
      return 0;
    }
  }

  const char *scopeStart = NULL;
  int nScope = 0;
  int afterScope = s;
  out->hasScope = parse_scope_prefix(text, s, e, &scopeStart, &nScope, &afterScope);
  if (out->hasScope) {
    s = afterScope;
    if (s >= e) {
      return 0;
    }
  }

  *sOut = s;
  *eOut = e;
  *scopeStartOut = scopeStart;
  *nScopeOut = nScope;
  return 1;
}

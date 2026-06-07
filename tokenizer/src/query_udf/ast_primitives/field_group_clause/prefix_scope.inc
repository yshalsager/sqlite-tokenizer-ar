static int parse_field_group_prefix_and_scope(
    const char *query,
    int nQuery,
    int *innerStartOut,
    const char **scopeOut,
    int *scopeLenOut,
    const char **occurOut,
    int *nOccurOut) {
  int spanStart = 0;
  int spanEnd = 0;
  trim_ascii_space_span(query, nQuery, &spanStart, &spanEnd);
  if (spanStart >= spanEnd) {
    return 0;
  }

  const char *occurText = "SHOULD";
  int nOccurText = 6;
  int idx = spanStart;
  if (query[idx] == '+' || query[idx] == '-') {
    if (query[idx] == '+') {
      occurText = "MUST";
      nOccurText = 4;
    } else {
      occurText = "MUST_NOT";
      nOccurText = 8;
    }
    idx++;
    while (idx < spanEnd && isspace((unsigned char)query[idx])) {
      idx++;
    }
    if (idx >= spanEnd) {
      return 0;
    }
  }

  int scopeStart = idx;
  while (idx < spanEnd && query[idx] != ':' && !isspace((unsigned char)query[idx])) {
    idx++;
  }
  if (idx >= spanEnd || query[idx] != ':') {
    return 0;
  }
  int scopeLen = idx - scopeStart;
  if (scopeLen <= 0) {
    return 0;
  }
  const char *scope = query + scopeStart;
  if (!(token_equals_ascii_ci(scope, scopeLen, "page") || token_equals_ascii_ci(scope, scopeLen, "title"))) {
    return 0;
  }

  idx++;
  if (idx >= spanEnd || query[idx] != '(') {
    return 0;
  }

  *innerStartOut = idx + 1;
  *scopeOut = scope;
  *scopeLenOut = scopeLen;
  *occurOut = occurText;
  *nOccurOut = nOccurText;
  return 1;
}

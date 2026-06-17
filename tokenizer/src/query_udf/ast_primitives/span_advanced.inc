static int parse_fuzzy_token_span(const char *token, int nToken, const char **base, int *nBase, int *edits) {
  int splitAt = -1;
  for (int idx = nToken - 1; idx >= 0; idx--) {
    if (token[idx] != '~') {
      continue;
    }
    if (!is_escaped_at(token, 0, idx)) {
      splitAt = idx;
      break;
    }
  }
  if (splitAt <= 0) {
    return 0;
  }

  const char *baseText = token;
  int nBaseText = splitAt;
  const char *rawEdits = token + splitAt + 1;
  int nRawEdits = nToken - (splitAt + 1);
  if (nRawEdits > 0) {
    for (int i = 0; i < nRawEdits; i++) {
      if (!isdigit((unsigned char)rawEdits[i])) {
        return 0;
      }
    }
  }

  int parsedEdits = 2;
  if (nRawEdits > 0) {
    if (!parse_nonnegative_int_span_clamped(rawEdits, nRawEdits, 2, &parsedEdits)) {
      return 0;
    }
  }
  if (parsedEdits < 0) {
    parsedEdits = 0;
  }
  if (parsedEdits > 2) {
    parsedEdits = 2;
  }

  *base = baseText;
  *nBase = nBaseText;
  *edits = parsedEdits;
  return 1;
}

static int parse_top_level_group_boost_span(
    const char *query,
    int nQuery,
    const char **inner,
    int *nInner,
    char *boostBuf,
    int boostBufCap,
    int *nBoost) {
  int start = 0;
  int end = 0;
  trim_ascii_space_span(query, nQuery, &start, &end);
  if ((end - start) < 4 || query[start] != '(') {
    return 0;
  }

  int depth = 0;
  int inQuote = 0;
  int closeIndex = -1;
  for (int idx = start; idx < end; idx++) {
    char ch = query[idx];
    if (ch == '"' && (!is_escaped_at(query, start, idx))) {
      inQuote = !inQuote;
      continue;
    }
    if (inQuote) {
      continue;
    }
    if (ch == '(') {
      depth++;
      continue;
    }
    if (ch == ')') {
      depth--;
      if (depth == 0) {
        closeIndex = idx;
        break;
      }
      if (depth < 0) {
        return 0;
      }
    }
  }
  if (closeIndex <= start) {
    return 0;
  }
  if (closeIndex + 2 > end || query[closeIndex + 1] != '^') {
    return 0;
  }

  int boostStart = closeIndex + 2;
  int boostEnd = 0;
  if (!parse_decimal_number_span(query, boostStart, end, &boostEnd) || boostEnd != end) {
    return 0;
  }

  int nRawBoost = boostEnd - boostStart;
  int nFmt = copy_boost_literal_span(query + boostStart, nRawBoost, boostBuf, boostBufCap, NULL);
  if (nFmt <= 0) {
    return 0;
  }

  *inner = query + start + 1;
  *nInner = closeIndex - (start + 1);
  *nBoost = nFmt;
  return 1;
}


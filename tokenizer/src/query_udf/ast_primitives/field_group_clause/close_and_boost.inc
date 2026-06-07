static int parse_field_group_close_index(
    const char *query,
    int nQuery,
    int innerStart,
    int *closeIdxOut,
    int *afterCloseOut) {
  int spanStart = 0;
  int spanEnd = 0;
  trim_ascii_space_span(query, nQuery, &spanStart, &spanEnd);
  if (spanStart >= spanEnd || innerStart <= spanStart || innerStart > spanEnd) {
    return 0;
  }

  int idx = innerStart;
  int depth = 1;
  int inQuote = 0;
  while (idx < spanEnd) {
    char ch = query[idx];
    if (ch == '\\' && idx + 1 < spanEnd) {
      idx += 2;
      continue;
    }
    if (ch == '"') {
      inQuote = !inQuote;
      idx++;
      continue;
    }
    if (!inQuote) {
      if (ch == '(') {
        depth++;
      } else if (ch == ')') {
        depth--;
        if (depth == 0) {
          break;
        }
      }
    }
    idx++;
  }
  if (idx >= spanEnd || depth != 0) {
    return 0;
  }

  *closeIdxOut = idx;
  *afterCloseOut = idx + 1;
  return 1;
}

static int parse_field_group_optional_boost(
    const char *query,
    int nQuery,
    int afterCloseIdx,
    const char **kindOut,
    int *nKindOut,
    char *boostBuf,
    int boostBufCap,
    int *boostLenOut) {
  int spanStart = 0;
  int spanEnd = 0;
  trim_ascii_space_span(query, nQuery, &spanStart, &spanEnd);
  if (spanStart >= spanEnd || afterCloseIdx < spanStart || afterCloseIdx > spanEnd) {
    return 0;
  }

  int idx = afterCloseIdx;
  while (idx < spanEnd && isspace((unsigned char)query[idx])) {
    idx++;
  }

  const char *nodeKind = "field_group";
  int nNodeKind = 11;
  int boostLen = 0;
  if (idx < spanEnd) {
    if (query[idx] != '^') {
      return 0;
    }
    idx++;
    int boostStart = idx;
    while (idx < spanEnd && (isdigit((unsigned char)query[idx]) || query[idx] == '.')) {
      idx++;
    }
    int boostEnd = idx;
    if (idx <= boostStart) {
      return 0;
    }
    while (idx < spanEnd && isspace((unsigned char)query[idx])) {
      idx++;
    }
    if (idx != spanEnd) {
      return 0;
    }

    char numBuf[64];
    int nNum = boostEnd - boostStart;
    if (nNum <= 0 || nNum >= (int)sizeof(numBuf)) {
      return 0;
    }
    memcpy(numBuf, query + boostStart, (size_t)nNum);
    numBuf[nNum] = '\0';
    char *endPtr = NULL;
    double boostValue = strtod(numBuf, &endPtr);
    if (endPtr == NULL || *endPtr != '\0') {
      return 0;
    }
    boostLen = format_float_like_python(boostValue, boostBuf, boostBufCap);
    if (boostLen <= 0) {
      return 0;
    }
    nodeKind = "field_group_boost";
    nNodeKind = 17;
  }

  *kindOut = nodeKind;
  *nKindOut = nNodeKind;
  *boostLenOut = boostLen;
  return 1;
}

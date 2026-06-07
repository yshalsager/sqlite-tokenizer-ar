static int parse_decimal_number_span(const char *text, int start, int end, int *outNext) {
  int idx = start;
  int intDigits = 0;
  while (idx < end && isdigit((unsigned char)text[idx])) {
    idx++;
    intDigits++;
  }
  if (intDigits == 0) {
    return 0;
  }
  if (idx < end && text[idx] == '.') {
    idx++;
    int fracDigits = 0;
    while (idx < end && isdigit((unsigned char)text[idx])) {
      idx++;
      fracDigits++;
    }
    if (fracDigits == 0) {
      return 0;
    }
  }
  *outNext = idx;
  return 1;
}

static int parse_quoted_text_unescaped(
    const char *text,
    int start,
    int end,
    char *out,
    int outCap,
    int *nOut,
    int *outNext) {
  if (start >= end || text[start] != '"') {
    return 0;
  }
  int inPos = start + 1;
  int outPos = 0;
  while (inPos < end) {
    char ch = text[inPos];
    if (ch == '\\' && inPos + 1 < end) {
      if (outPos + 1 >= outCap) {
        return 0;
      }
      out[outPos++] = text[inPos + 1];
      inPos += 2;
      continue;
    }
    if (ch == '"') {
      out[outPos] = '\0';
      *nOut = outPos;
      *outNext = inPos + 1;
      return 1;
    }
    if (outPos + 1 >= outCap) {
      return 0;
    }
    out[outPos++] = ch;
    inPos++;
  }
  return 0;
}

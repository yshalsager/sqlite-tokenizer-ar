static int token_equals_ascii_ci(const char *token, int nToken, const char *literal) {
  int nLit = (int)strlen(literal);
  if (nToken != nLit) {
    return 0;
  }
  for (int i = 0; i < nToken; i++) {
    unsigned char left = (unsigned char)token[i];
    unsigned char right = (unsigned char)literal[i];
    if ((unsigned char)toupper(left) != (unsigned char)toupper(right)) {
      return 0;
    }
  }
  return 1;
}

static void trim_ascii_space_span(const char *text, int nText, int *start, int *end) {
  int s = 0;
  int e = nText;
  while (s < e && isspace((unsigned char)text[s])) {
    s++;
  }
  while (e > s && isspace((unsigned char)text[e - 1])) {
    e--;
  }
  *start = s;
  *end = e;
}

static int format_float_like_python(double value, char *out, int outCap) {
  int n = sqlite3_snprintf(outCap, out, "%.15g", value) ? (int)strlen(out) : 0;
  if (n <= 0 || n >= outCap) {
    return 0;
  }
  int hasDotOrExp = 0;
  for (int i = 0; i < n; i++) {
    char ch = out[i];
    if (ch == '.' || ch == 'e' || ch == 'E') {
      hasDotOrExp = 1;
      break;
    }
  }
  if (!hasDotOrExp) {
    if (n + 2 >= outCap) {
      return 0;
    }
    out[n++] = '.';
    out[n++] = '0';
    out[n] = '\0';
  }
  return n;
}

static int split_ascii_space_tokens(const char *text, int nText, const char **starts, int *lengths, int maxTokens, int *outCount) {
  int count = 0;
  int idx = 0;
  while (idx < nText) {
    while (idx < nText && isspace((unsigned char)text[idx])) {
      idx++;
    }
    if (idx >= nText) {
      break;
    }
    if (count >= maxTokens) {
      return SQLITE_TOOBIG;
    }
    int start = idx;
    while (idx < nText && !isspace((unsigned char)text[idx])) {
      idx++;
    }
    starts[count] = text + start;
    lengths[count] = idx - start;
    count++;
  }
  *outCount = count;
  return SQLITE_OK;
}

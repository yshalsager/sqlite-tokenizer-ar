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


static int parse_nonnegative_int_span(const char *text, int nText, int maxValue, int *outValue) {
  if (text == NULL || nText <= 0 || maxValue < 0) {
    return 0;
  }
  int value = 0;
  for (int i = 0; i < nText; i++) {
    if (!isdigit((unsigned char)text[i])) {
      return 0;
    }
    int digit = text[i] - '0';
    if (value > maxValue / 10 || (value == maxValue / 10 && digit > maxValue % 10)) {
      return 0;
    }
    value = value * 10 + digit;
  }
  *outValue = value;
  return 1;
}

static int parse_nonnegative_int_span_clamped(const char *text, int nText, int maxValue, int *outValue) {
  if (text == NULL || nText <= 0 || maxValue < 0) {
    return 0;
  }
  int value = 0;
  int saturated = 0;
  for (int i = 0; i < nText; i++) {
    if (!isdigit((unsigned char)text[i])) {
      return 0;
    }
    if (saturated) {
      continue;
    }
    int digit = text[i] - '0';
    if (value > maxValue / 10 || (value == maxValue / 10 && digit > maxValue % 10)) {
      value = maxValue;
      saturated = 1;
    } else {
      value = value * 10 + digit;
    }
  }
  *outValue = value;
  return 1;
}

#define QUERY_BOOST_LITERAL_MAX_BYTES 63
#define QUERY_DEFAULT_BOOST_LITERAL "1.0"
#define QUERY_DEFAULT_BOOST_LITERAL_LEN 3

static int is_boost_literal_shape(const char *text, int nText) {
  if (text == NULL || nText <= 0 || nText > QUERY_BOOST_LITERAL_MAX_BYTES) {
    return 0;
  }
  int idx = 0;
  while (idx < nText && isdigit((unsigned char)text[idx])) {
    idx++;
  }
  if (idx == 0) {
    return 0;
  }
  if (idx < nText && text[idx] == '.') {
    idx++;
    int fracStart = idx;
    while (idx < nText && isdigit((unsigned char)text[idx])) {
      idx++;
    }
    if (idx == fracStart) {
      return 0;
    }
  }
  return idx == nText;
}

static int is_query_boost_boundary(const char *text, int idx, int end) {
  if (idx >= end) {
    return 1;
  }
  unsigned char ch = (unsigned char)text[idx];
  return isspace(ch) || ch == ')';
}

static int parse_boost_literal_double_span(const char *text, int nText, double *outValue) {
  if (!is_boost_literal_shape(text, nText)) {
    return 0;
  }
  char buf[QUERY_BOOST_LITERAL_MAX_BYTES + 1];
  memcpy(buf, text, (size_t)nText);
  buf[nText] = '\0';

  errno = 0;
  char *endPtr = NULL;
  double value = strtod(buf, &endPtr);
  if (endPtr == buf || endPtr == NULL || *endPtr != '\0' || errno == ERANGE || !isfinite(value)) {
    return 0;
  }
  *outValue = value;
  return 1;
}

static int parse_boost_literal_double_cstr(const char *text, double *outValue) {
  if (text == NULL) {
    return 0;
  }
  size_t nText = strlen(text);
  if (nText > QUERY_BOOST_LITERAL_MAX_BYTES) {
    return 0;
  }
  return parse_boost_literal_double_span(text, (int)nText, outValue);
}

static int copy_boost_literal_span(
    const char *text,
    int nText,
    char *out,
    int outCap,
    double *outValue) {
  double value = 0.0;
  if (!parse_boost_literal_double_span(text, nText, &value) || out == NULL || nText >= outCap) {
    return 0;
  }
  memcpy(out, text, (size_t)nText);
  out[nText] = '\0';
  if (outValue != NULL) {
    *outValue = value;
  }
  return nText;
}

static int copy_default_boost_literal(char *out, int outCap) {
  if (out == NULL || QUERY_DEFAULT_BOOST_LITERAL_LEN >= outCap) {
    return 0;
  }
  memcpy(out, QUERY_DEFAULT_BOOST_LITERAL, QUERY_DEFAULT_BOOST_LITERAL_LEN + 1);
  return QUERY_DEFAULT_BOOST_LITERAL_LEN;
}

static int json_builder_append_boost_literal(
    char **json,
    int *jsonLen,
    int *jsonCap,
    const char *text,
    int nText) {
  if (text == NULL || nText <= 0) {
    return SQLITE_ERROR;
  }
  int intEnd = 0;
  while (intEnd < nText && isdigit((unsigned char)text[intEnd])) {
    intEnd++;
  }
  int firstDigit = 0;
  while (firstDigit + 1 < intEnd && text[firstDigit] == '0') {
    firstDigit++;
  }
  int rc = json_builder_append_bytes(json, jsonLen, jsonCap, text + firstDigit, intEnd - firstDigit);
  if (rc == SQLITE_OK && intEnd < nText) {
    rc = json_builder_append_bytes(json, jsonLen, jsonCap, text + intEnd, nText - intEnd);
  }
  return rc;
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

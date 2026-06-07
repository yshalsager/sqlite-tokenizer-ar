static int is_simple_boolean_token_plain(const char *token, int nToken) {
  if (token == NULL || nToken <= 0) {
    return 0;
  }
  for (int i = 0; i < nToken; i++) {
    unsigned char ch = (unsigned char)token[i];
    if (isspace((int)ch)) {
      return 0;
    }
    if (ch == '"' || ch == '(' || ch == ')' || ch == ':' || ch == '*' || ch == '?' || ch == '~' || ch == '^' || ch == '+' ||
        ch == '-') {
      return 0;
    }
  }
  return 1;
}

static int parse_simple_boolean_operand(
    const char *token,
    int nToken,
    const char **outBase,
    int *outNBase,
    char *outBoostBuf,
    int outBoostCap,
    int *outNBoost) {
  if (is_simple_boolean_token_plain(token, nToken)) {
    *outBase = token;
    *outNBase = nToken;
    *outNBoost = format_float_like_python(1.0, outBoostBuf, outBoostCap);
    return (*outNBoost > 0);
  }

  int splitAt = -1;
  for (int idx = nToken - 1; idx >= 0; idx--) {
    if (token[idx] != '^') {
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

  int boostStart = splitAt + 1;
  int boostEnd = 0;
  if (!parse_decimal_number_span(token, boostStart, nToken, &boostEnd) || boostEnd != nToken) {
    return 0;
  }
  if (!is_simple_boolean_token_plain(token, splitAt)) {
    return 0;
  }

  char numBuf[64];
  int nNum = nToken - boostStart;
  if (nNum <= 0 || nNum >= (int)sizeof(numBuf)) {
    return 0;
  }
  memcpy(numBuf, token + boostStart, (size_t)nNum);
  numBuf[nNum] = '\0';
  char *endPtr = NULL;
  double boostValue = strtod(numBuf, &endPtr);
  if (endPtr == NULL || *endPtr != '\0') {
    return 0;
  }
  int nBoost = format_float_like_python(boostValue, outBoostBuf, outBoostCap);
  if (nBoost <= 0) {
    return 0;
  }

  *outBase = token;
  *outNBase = splitAt;
  *outNBoost = nBoost;
  return 1;
}

static void udf_sqlite_tokenizer_ar_parse_simple_boolean_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_parse_simple_boolean_json expects 1 argument", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  const char *query = (const char *)sqlite3_value_text(argv[0]);
  int nQuery = sqlite3_value_bytes(argv[0]);
  if (query == NULL || nQuery <= 0) {
    sqlite3_result_null(context);
    return;
  }

  int start = 0;
  while (start < nQuery && isspace((unsigned char)query[start])) {
    start++;
  }
  int end = nQuery;
  while (end > start && isspace((unsigned char)query[end - 1])) {
    end--;
  }
  if (end <= start) {
    sqlite3_result_null(context);
    return;
  }
  int nTrimmed = end - start;
  const char *trimmed = query + start;
  for (int i = 0; i < nTrimmed; i++) {
    if (trimmed[i] == '"' || trimmed[i] == '(' || trimmed[i] == ')') {
      sqlite3_result_null(context);
      return;
    }
  }

  int i = 0;
  while (i < nTrimmed && isspace((unsigned char)trimmed[i])) {
    i++;
  }
  int leftStart = i;
  while (i < nTrimmed && !isspace((unsigned char)trimmed[i])) {
    i++;
  }
  int nLeft = i - leftStart;
  while (i < nTrimmed && isspace((unsigned char)trimmed[i])) {
    i++;
  }
  int opStart = i;
  while (i < nTrimmed && !isspace((unsigned char)trimmed[i])) {
    i++;
  }
  int nOp = i - opStart;
  while (i < nTrimmed && isspace((unsigned char)trimmed[i])) {
    i++;
  }
  int rightStart = i;
  while (i < nTrimmed && !isspace((unsigned char)trimmed[i])) {
    i++;
  }
  int nRight = i - rightStart;
  while (i < nTrimmed && isspace((unsigned char)trimmed[i])) {
    i++;
  }
  if (nLeft <= 0 || nOp <= 0 || nRight <= 0 || i != nTrimmed) {
    sqlite3_result_null(context);
    return;
  }

  const char *op = trimmed + opStart;
  if (!(token_equals_ascii_ci(op, nOp, "AND") || token_equals_ascii_ci(op, nOp, "OR"))) {
    sqlite3_result_null(context);
    return;
  }

  const char *leftBase = NULL;
  const char *rightBase = NULL;
  int nLeftBase = 0;
  int nRightBase = 0;
  char leftBoostBuf[64];
  char rightBoostBuf[64];
  int nLeftBoost = 0;
  int nRightBoost = 0;
  if (!parse_simple_boolean_operand(
          trimmed + leftStart,
          nLeft,
          &leftBase,
          &nLeftBase,
          leftBoostBuf,
          (int)sizeof(leftBoostBuf),
          &nLeftBoost) ||
      !parse_simple_boolean_operand(
          trimmed + rightStart,
          nRight,
          &rightBase,
          &nRightBase,
          rightBoostBuf,
          (int)sizeof(rightBoostBuf),
          &nRightBoost)) {
    sqlite3_result_null(context);
    return;
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "{\"op\":", 6);
  if (rc == SQLITE_OK) {
    if (token_equals_ascii_ci(op, nOp, "AND")) {
      rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "\"AND\"", 5);
    } else {
      rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "\"OR\"", 4);
    }
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"left\":", 8);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, leftBase, nLeftBase);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"left_boost\":", 14);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, leftBoostBuf, nLeftBoost);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"right\":", 9);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, rightBase, nRightBase);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"right_boost\":", 15);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, rightBoostBuf, nRightBoost);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&json, &jsonLen, &jsonCap, '}');
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return;
  }
  json[jsonLen] = '\0';
  sqlite3_result_text(context, json, jsonLen, sqlite3_free);
}

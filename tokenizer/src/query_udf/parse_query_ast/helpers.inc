#define PARSE_QUERY_AST_UNSUPPORTED_MSG \
  "C AST backend is not available: unsupported query shape for current C parser rollout"

static void result_parse_query_ast_unsupported(sqlite3_context *context) {
  sqlite3_result_error(context, PARSE_QUERY_AST_UNSUPPORTED_MSG, -1);
}

static int parse_query_ast_is_dangling_field_scope(const char *query, int nQuery) {
  int start = 0;
  int end = 0;
  trim_ascii_space_span(query, nQuery, &start, &end);
  if (start >= end) {
    return 0;
  }

  int s = start;
  if (query[s] == '+' || query[s] == '-') {
    s++;
    while (s < end && isspace((unsigned char)query[s])) {
      s++;
    }
  }
  if ((end - s) >= 3 && token_equals_ascii_ci(query + s, 3, "NOT") && (s + 3 == end || isspace((unsigned char)query[s + 3]))) {
    s += 3;
    while (s < end && isspace((unsigned char)query[s])) {
      s++;
    }
  }
  if (s >= end) {
    return 0;
  }

  int remaining = end - s;
  if (remaining == 5 && token_equals_ascii_ci(query + s, 5, "page:")) {
    return 1;
  }
  if (remaining == 6 && token_equals_ascii_ci(query + s, 6, "title:")) {
    return 1;
  }
  return 0;
}

static int parse_query_ast_is_ascii_boolean_keyword(const char *token, int nToken) {
  if (token == NULL || nToken <= 0) {
    return 0;
  }
  return token_equals_ascii_ci(token, nToken, "AND") || token_equals_ascii_ci(token, nToken, "OR") ||
         token_equals_ascii_ci(token, nToken, "NOT");
}

static int parse_query_ast_has_unclosed_field_group(const char *query, int nQuery) {
  if (query == NULL || nQuery <= 0) {
    return 0;
  }
  for (int i = 0; i < nQuery; i++) {
    if (query[i] == '\\' && i + 1 < nQuery) {
      i++;
      continue;
    }
    if (i + 5 < nQuery && token_equals_ascii_ci(query + i, 5, "page:") && query[i + 5] == '(') {
      return 1;
    }
    if (i + 6 < nQuery && token_equals_ascii_ci(query + i, 6, "title:") && query[i + 6] == '(') {
      return 1;
    }
  }
  return 0;
}

static int parse_query_ast_is_dangling_boolean_operator(const char *query, int nQuery) {
  int start = 0;
  int end = 0;
  trim_ascii_space_span(query, nQuery, &start, &end);
  if (start >= end) {
    return 0;
  }

  int firstEnd = start;
  while (firstEnd < end && !isspace((unsigned char)query[firstEnd])) {
    firstEnd++;
  }
  int lastStart = end - 1;
  while (lastStart >= start && isspace((unsigned char)query[lastStart])) {
    lastStart--;
  }
  int lastEnd = lastStart + 1;
  while (lastStart >= start && !isspace((unsigned char)query[lastStart])) {
    lastStart--;
  }
  lastStart++;
  if (firstEnd == end && parse_query_ast_is_ascii_boolean_keyword(query + start, end - start)) {
    return 1;
  }
  if (parse_query_ast_is_ascii_boolean_keyword(query + lastStart, lastEnd - lastStart)) {
    return 1;
  }
  return 0;
}

static const char *parse_query_ast_classify_malformed_query(const char *query, int nQuery) {
  int start = 0;
  int end = 0;
  trim_ascii_space_span(query, nQuery, &start, &end);
  if (start >= end) {
    return NULL;
  }
  if (parse_query_ast_is_dangling_field_scope(query, nQuery)) {
    return "dangling field scope";
  }

  int inQuote = 0;
  int depth = 0;
  for (int i = start; i < end; i++) {
    char ch = query[i];
    if (ch == '\\' && i + 1 < end) {
      i++;
      continue;
    }
    if (ch == '"') {
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
      if (depth <= 0) {
        return "unmatched closing parenthesis";
      }
      depth--;
    }
  }
  if (inQuote) {
    return "unclosed quote in query";
  }
  if (depth > 0) {
    if (parse_query_ast_has_unclosed_field_group(query + start, end - start)) {
      return "unclosed field-group parentheses in query";
    }
    return "unclosed group in query";
  }
  if (parse_query_ast_is_dangling_boolean_operator(query + start, end - start)) {
    return "dangling boolean operator";
  }
  return NULL;
}

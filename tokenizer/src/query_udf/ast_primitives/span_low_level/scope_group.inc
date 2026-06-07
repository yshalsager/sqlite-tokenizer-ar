static int parse_scope_prefix(const char *text, int start, int end, const char **scopeStart, int *nScope, int *afterScope) {
  int remaining = end - start;
  if (remaining >= 5 && token_equals_ascii_ci(text + start, 4, "page") && text[start + 4] == ':') {
    *scopeStart = text + start;
    *nScope = 4;
    *afterScope = start + 5;
    return 1;
  }
  if (remaining >= 6 && token_equals_ascii_ci(text + start, 5, "title") && text[start + 5] == ':') {
    *scopeStart = text + start;
    *nScope = 5;
    *afterScope = start + 6;
    return 1;
  }
  return 0;
}

static int find_matching_group_close(const char *text, int start, int end, int *outClose) {
  if (start >= end || text[start] != '(') {
    return 0;
  }
  int idx = start + 1;
  int depth = 1;
  int inQuote = 0;
  while (idx < end) {
    char ch = text[idx];
    if (ch == '\\' && idx + 1 < end) {
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
          *outClose = idx;
          return 1;
        }
      }
    }
    idx++;
  }
  return 0;
}

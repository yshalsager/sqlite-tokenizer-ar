static int find_simple_top_level_operator(const char *text, int start, int end, int *opStart, int *opEnd, int *opKind) {
  int inQuote = 0;
  int depth = 0;
  int foundStart = -1;
  int foundEnd = -1;
  int foundKind = 0;
  int idx = start;
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
    if (inQuote) {
      idx++;
      continue;
    }
    if (ch == '(') {
      depth++;
      idx++;
      continue;
    }
    if (ch == ')') {
      depth--;
      if (depth < 0) {
        return -1;
      }
      idx++;
      continue;
    }
    if (depth > 0) {
      idx++;
      continue;
    }
    if (!isalpha((unsigned char)ch)) {
      idx++;
      continue;
    }
    int atWordStart = (idx == start) || isspace((unsigned char)text[idx - 1]);
    if (!atWordStart) {
      idx++;
      continue;
    }

    int localKind = 0;
    int localLen = 0;
    if ((idx + 3) <= end && token_equals_ascii_ci(text + idx, 3, "AND")) {
      localKind = 1;
      localLen = 3;
    } else if ((idx + 2) <= end && token_equals_ascii_ci(text + idx, 2, "OR")) {
      localKind = 2;
      localLen = 2;
    } else if ((idx + 3) <= end && token_equals_ascii_ci(text + idx, 3, "NOT")) {
      localKind = 3;
      localLen = 3;
    }
    if (localKind == 0) {
      idx++;
      continue;
    }

    int atWordEnd = (idx + localLen == end) || isspace((unsigned char)text[idx + localLen]);
    if (!atWordEnd) {
      idx++;
      continue;
    }
    if (foundKind != 0) {
      if (localKind == 3 && (foundKind == 1 || foundKind == 2)) {
        idx += localLen;
        continue;
      }
      if (localKind == foundKind) {
        idx += localLen;
        continue;
      }
      if ((foundKind == 1 || foundKind == 2) && (localKind == 1 || localKind == 2)) {
        /* Mixed AND/OR precedence: OR is the lower-precedence split point. */
        if (foundKind == 1 && localKind == 2) {
          foundStart = idx;
          foundEnd = idx + localLen;
          foundKind = localKind;
        }
        idx += localLen;
        continue;
      }
      return -1;
    }
    foundStart = idx;
    foundEnd = idx + localLen;
    foundKind = localKind;
    idx = foundEnd;
    continue;
  }
  if (inQuote) {
    return -1;
  }
  if (depth != 0) {
    return -1;
  }
  *opStart = foundStart;
  *opEnd = foundEnd;
  *opKind = foundKind;
  return 1;
}

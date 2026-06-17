static int find_first_top_level_operand_end(const char *text, int start, int end, int *outEnd) {
  int inQuote = 0;
  int depth = 0;
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
    if (!inQuote) {
      if (ch == '(') {
        depth++;
        idx++;
        continue;
      }
      if (ch == ')') {
        if (depth <= 0) {
          return 0;
        }
        depth--;
        idx++;
        continue;
      }
      if (depth == 0 && isspace((unsigned char)ch)) {
        break;
      }
    }
    idx++;
  }
  if (inQuote || depth != 0) {
    return 0;
  }
  if (idx <= start) {
    return 0;
  }
  *outEnd = idx;
  return 1;
}

static int parse_simple_query_ast_clauses(
    const char *query,
    int nQuery,
    ParsedSimpleAstClause *left,
    ParsedSimpleAstClause *right,
    int *outCount) {
  int start = 0;
  int end = 0;
  trim_ascii_space_span(query, nQuery, &start, &end);
  if (start >= end) {
    *outCount = 0;
    return 1;
  }

  int opStart = -1;
  int opEnd = -1;
  int opKind = 0;
  int leftFallbackGroup = 0;
  int rightFallbackGroup = 0;
  int opRc = find_simple_top_level_operator(query, start, end, &opStart, &opEnd, &opKind);
  if (opRc < 0) {
    return 0;
  }

  if (opKind == 0) {
    if (parse_simple_operand_clause(query, start, end, left)) {
      if (left->nKind == 5 && !left->hasScope &&
          (token_equals_ascii_ci(left->value, left->nValue, "AND") || token_equals_ascii_ci(left->value, left->nValue, "OR") ||
           token_equals_ascii_ci(left->value, left->nValue, "NOT"))) {
        return 0;
      }
      *outCount = 1;
      return 1;
    }

    int firstEnd = 0;
    if (!find_first_top_level_operand_end(query, start, end, &firstEnd)) {
      return 0;
    }
    if (!parse_simple_operand_clause(query, start, firstEnd, left)) {
      return 0;
    }
    if (left->nKind == 5 && !left->hasScope &&
        (token_equals_ascii_ci(left->value, left->nValue, "AND") || token_equals_ascii_ci(left->value, left->nValue, "OR") ||
         token_equals_ascii_ci(left->value, left->nValue, "NOT"))) {
      return 0;
    }

    int restStart = firstEnd;
    while (restStart < end && isspace((unsigned char)query[restStart])) {
      restStart++;
    }
    if (restStart >= end) {
      return 0;
    }

    if (parse_simple_operand_clause(query, restStart, end, right)) {
      *outCount = 2;
      return 1;
    }

    int nRest = end - restStart;
    if (nRest <= 0 || nRest >= (int)sizeof(right->value)) {
      return 0;
    }
    right->occur = "SHOULD";
    right->nOccur = 6;
    right->kind = "group";
    right->nKind = 5;
    right->hasScope = 0;
    memcpy(right->value, query + restStart, (size_t)nRest);
    right->value[nRest] = '\0';
    right->nValue = nRest;
    *outCount = 2;
    return 1;
  }

  if (!parse_simple_operand_clause(query, start, opStart, left)) {
    int leftStart = start;
    int leftEnd = opStart;
    trim_ascii_space_span(query, opStart, &leftStart, &leftEnd);
    int nLeft = leftEnd - leftStart;
    if (nLeft <= 0 || nLeft >= (int)sizeof(left->value)) {
      return 0;
    }
    left->occur = "SHOULD";
    left->nOccur = 6;
    left->kind = "group";
    left->nKind = 5;
    left->hasScope = 0;
    memcpy(left->value, query + leftStart, (size_t)nLeft);
    left->value[nLeft] = '\0';
    left->nValue = nLeft;
    leftFallbackGroup = 1;
  }
  if (!parse_simple_operand_clause(query, opEnd, end, right)) {
    int restStart = opEnd;
    while (restStart < end && isspace((unsigned char)query[restStart])) {
      restStart++;
    }
    int nRest = end - restStart;
    if (nRest <= 0 || nRest >= (int)sizeof(right->value)) {
      return 0;
    }
    right->occur = "SHOULD";
    right->nOccur = 6;
    right->kind = "group";
    right->nKind = 5;
    right->hasScope = 0;
    memcpy(right->value, query + restStart, (size_t)nRest);
    right->value[nRest] = '\0';
    right->nValue = nRest;
    rightFallbackGroup = 1;
  }

  if (opKind == 1) { /* AND */
    if (left->nOccur == 6) {
      left->occur = "MUST";
      left->nOccur = 4;
    }
    if (right->nOccur == 6) {
      right->occur = "MUST";
      right->nOccur = 4;
    }
  } else if (opKind == 3) { /* NOT */
    right->occur = "MUST_NOT";
    right->nOccur = 8;
  } else if (opKind == 2) { /* OR */
    int innerStart = 0;
    int innerEnd = 0;
    int innerKind = 0;
    if (leftFallbackGroup && find_simple_top_level_operator(left->value, 0, left->nValue, &innerStart, &innerEnd, &innerKind) >= 0 &&
        innerKind == 1) {
      left->occur = "MUST";
      left->nOccur = 4;
    }
    innerStart = 0;
    innerEnd = 0;
    innerKind = 0;
    if (rightFallbackGroup && find_simple_top_level_operator(right->value, 0, right->nValue, &innerStart, &innerEnd, &innerKind) >= 0 &&
        innerKind == 1) {
      right->occur = "MUST";
      right->nOccur = 4;
    }
  }

  *outCount = 2;
  return 1;
}

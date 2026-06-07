static int parse_simple_operand_token_clause(
    const char *text,
    int s,
    int e,
    ParsedSimpleAstClause *out,
    const char *scopeStart,
    int nScope) {
  for (int idx = s; idx < e; idx++) {
    if (text[idx] == '\\' && idx + 1 < e) {
      idx++;
      continue;
    }
    if (isspace((unsigned char)text[idx])) {
      return 0;
    }
    if (text[idx] == '(' || text[idx] == ')' || text[idx] == '\t') {
      return 0;
    }
  }
  int tokenLen = e - s;
  if (tokenLen <= 0) {
    return 0;
  }

  int outPos = 0;
  if (out->hasScope) {
    if (outPos + nScope + 1 >= (int)sizeof(out->value)) {
      return 0;
    }
    memcpy(out->value + outPos, scopeStart, (size_t)nScope);
    outPos += nScope;
    out->value[outPos++] = ':';
  }
  if (outPos + tokenLen >= (int)sizeof(out->value)) {
    return 0;
  }
  memcpy(out->value + outPos, text + s, (size_t)tokenLen);
  outPos += tokenLen;
  out->value[outPos] = '\0';
  out->nValue = outPos;
  out->kind = "token";
  out->nKind = 5;
  return 1;
}

static int parse_simple_operand_group_clause(
    const char *text,
    int s,
    int e,
    ParsedSimpleAstClause *out,
    const char *scopeStart,
    int nScope) {
  int closeIdx = 0;
  if (!find_matching_group_close(text, s, e, &closeIdx)) {
    return 0;
  }
  int idx = closeIdx + 1;
  int hasBoost = 0;
  char boostBuf[64];
  int boostLen = 0;
  if (idx < e && text[idx] == '^') {
    hasBoost = 1;
    idx++;
    int boostEnd = 0;
    if (!parse_decimal_number_span(text, idx, e, &boostEnd)) {
      return 0;
    }
    int nBoost = boostEnd - idx;
    boostLen = copy_boost_literal_span(text + idx, nBoost, boostBuf, (int)sizeof(boostBuf), NULL);
    if (boostLen <= 0) {
      return 0;
    }
    idx = boostEnd;
  }
  while (idx < e && isspace((unsigned char)text[idx])) {
    idx++;
  }
  if (idx != e) {
    return 0;
  }
  int innerStart = s + 1;
  int innerLen = closeIdx - innerStart;
  if (innerLen <= 0) {
    return 0;
  }

  int outPos = 0;
  if (out->hasScope) {
    if (outPos + nScope + 1 >= (int)sizeof(out->value)) {
      return 0;
    }
    memcpy(out->value + outPos, scopeStart, (size_t)nScope);
    outPos += nScope;
    out->value[outPos++] = '\t';
    if (hasBoost) {
      if (outPos + boostLen + 1 >= (int)sizeof(out->value)) {
        return 0;
      }
      memcpy(out->value + outPos, boostBuf, (size_t)boostLen);
      outPos += boostLen;
      out->value[outPos++] = '\t';
    }
    if (outPos + innerLen >= (int)sizeof(out->value)) {
      return 0;
    }
    memcpy(out->value + outPos, text + innerStart, (size_t)innerLen);
    outPos += innerLen;
    out->value[outPos] = '\0';
    out->nValue = outPos;
    if (hasBoost) {
      out->kind = "field_group_boost";
      out->nKind = 17;
    } else {
      out->kind = "field_group";
      out->nKind = 11;
    }
    return 1;
  }

  if (outPos + innerLen >= (int)sizeof(out->value)) {
    return 0;
  }
  memcpy(out->value + outPos, text + innerStart, (size_t)innerLen);
  outPos += innerLen;
  out->value[outPos] = '\0';
  out->nValue = outPos;
  out->kind = "group";
  out->nKind = 5;
  return 1;
}

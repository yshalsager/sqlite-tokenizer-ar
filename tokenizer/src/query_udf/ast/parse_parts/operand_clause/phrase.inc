static int parse_simple_operand_phrase_clause(
    const char *text,
    int s,
    int e,
    ParsedSimpleAstClause *out,
    const char *scopeStart,
    int nScope) {
  char phrase[8192];
  int nPhrase = 0;
  int afterQuote = 0;
  if (!parse_quoted_text_unescaped(text, s, e, phrase, (int)sizeof(phrase), &nPhrase, &afterQuote)) {
    return 0;
  }
  int idx = afterQuote;
  int hasSlop = 0;
  int slopValue = 0;
  if (idx < e && text[idx] == '~') {
    hasSlop = 1;
    idx++;
    int slopEnd = 0;
    if (!parse_decimal_number_span(text, idx, e, &slopEnd)) {
      return 0;
    }
    if (slopEnd <= idx) {
      return 0;
    }
    int nSlopRaw = slopEnd - idx;
    if (!parse_nonnegative_int_span(text + idx, nSlopRaw, INT_MAX, &slopValue)) {
      return 0;
    }
    idx = slopEnd;
  }
  if (idx < e && text[idx] == '^') {
    idx++;
    int boostEnd = 0;
    if (!parse_decimal_number_span(text, idx, e, &boostEnd)) {
      return 0;
    }
    if (!is_boost_literal_shape(text + idx, boostEnd - idx)) {
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

  int outPos = 0;
  if (out->hasScope) {
    if (outPos + nScope + 1 >= (int)sizeof(out->value)) {
      return 0;
    }
    memcpy(out->value + outPos, scopeStart, (size_t)nScope);
    outPos += nScope;
    out->value[outPos++] = ':';
  }
  if (hasSlop) {
    char slopOut[32];
    int nSlopOut = sqlite3_snprintf((int)sizeof(slopOut), slopOut, "%d", slopValue) ? (int)strlen(slopOut) : 0;
    if (nSlopOut <= 0 || outPos + nSlopOut + 1 >= (int)sizeof(out->value)) {
      return 0;
    }
    memcpy(out->value + outPos, slopOut, (size_t)nSlopOut);
    outPos += nSlopOut;
    out->value[outPos++] = '\t';
  }
  if (outPos + nPhrase >= (int)sizeof(out->value)) {
    return 0;
  }
  if (nPhrase > 0) {
    memcpy(out->value + outPos, phrase, (size_t)nPhrase);
    outPos += nPhrase;
  }
  out->value[outPos] = '\0';
  out->nValue = outPos;

  if (!out->hasScope && !hasSlop) {
    out->kind = "phrase";
    out->nKind = 6;
  } else if (!out->hasScope && hasSlop) {
    out->kind = "phrase_slop";
    out->nKind = 11;
  } else if (out->hasScope && !hasSlop) {
    out->kind = "field_phrase";
    out->nKind = 12;
  } else {
    out->kind = "field_phrase_slop";
    out->nKind = 17;
  }
  return 1;
}

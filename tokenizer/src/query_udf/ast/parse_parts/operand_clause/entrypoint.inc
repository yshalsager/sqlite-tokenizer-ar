static int parse_simple_operand_clause(const char *text, int start, int end, ParsedSimpleAstClause *out) {
  int s = start;
  int e = end;
  const char *scopeStart = NULL;
  int nScope = 0;
  if (!parse_simple_operand_prefix(text, start, end, out, &s, &e, &scopeStart, &nScope)) {
    return 0;
  }

  if (text[s] == '(') {
    return parse_simple_operand_group_clause(text, s, e, out, scopeStart, nScope);
  }
  if (text[s] == '"') {
    return parse_simple_operand_phrase_clause(text, s, e, out, scopeStart, nScope);
  }
  return parse_simple_operand_token_clause(text, s, e, out, scopeStart, nScope);
}

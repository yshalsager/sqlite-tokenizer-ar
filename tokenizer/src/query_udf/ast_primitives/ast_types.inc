typedef struct ParsedSimpleAstClause {
  const char *occur;
  int nOccur;
  const char *kind;
  int nKind;
  int hasScope;
  char value[8192];
  int nValue;
} ParsedSimpleAstClause;


static int occur_equals(const char *occur, int nOccur, const char *literal) {
  int nLit = (int)strlen(literal);
  return (nOccur == nLit) && (memcmp(occur, literal, (size_t)nOccur) == 0);
}

static void compose_clause_occur_text(
    const char *parentOccur,
    int nParentOccur,
    const char *childOccur,
    int nChildOccur,
    const char **outOccur,
    int *nOutOccur) {
  if (parentOccur == NULL || nParentOccur <= 0) {
    *outOccur = childOccur;
    *nOutOccur = nChildOccur;
    return;
  }
  if (occur_equals(parentOccur, nParentOccur, "MUST_NOT")) {
    *outOccur = "MUST_NOT";
    *nOutOccur = 8;
    return;
  }
  if (occur_equals(parentOccur, nParentOccur, "MUST")) {
    if (occur_equals(childOccur, nChildOccur, "MUST_NOT")) {
      *outOccur = "MUST_NOT";
      *nOutOccur = 8;
      return;
    }
    *outOccur = childOccur;
    *nOutOccur = nChildOccur;
    return;
  }
  *outOccur = childOccur;
  *nOutOccur = nChildOccur;
}

static int write_field_group_value_to_scratch(
    const char *query,
    const char *scope,
    int scopeLen,
    int innerStart,
    int innerLen,
    const char *boostBuf,
    int boostLen,
    char *scratch,
    int scratchCap,
    const char **valueStartOut,
    int *nValueOut) {
  int outPos = 0;
  if (scopeLen + 1 >= scratchCap) {
    return 0;
  }
  memcpy(scratch + outPos, scope, (size_t)scopeLen);
  outPos += scopeLen;
  scratch[outPos++] = '\t';
  if (boostLen > 0) {
    if (outPos + boostLen + 1 >= scratchCap) {
      return 0;
    }
    memcpy(scratch + outPos, boostBuf, (size_t)boostLen);
    outPos += boostLen;
    scratch[outPos++] = '\t';
  }
  if (outPos + innerLen >= scratchCap) {
    return 0;
  }
  if (innerLen > 0) {
    memcpy(scratch + outPos, query + innerStart, (size_t)innerLen);
    outPos += innerLen;
  }
  scratch[outPos] = '\0';

  *valueStartOut = scratch;
  *nValueOut = outPos;
  return 1;
}

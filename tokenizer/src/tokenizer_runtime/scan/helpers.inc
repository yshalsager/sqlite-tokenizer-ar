static int emit_normalized_token_chunks(
    void *pCtx,
    int emitPositions,
    int start,
    int end,
    char *normalized,
    int normLen,
    int *logicalPosition,
    int (*xToken)(void *pCtx, int tflags, const char *pToken, int nToken, int iStart, int iEnd)) {
  int rc = SQLITE_OK;
  int chunkStart = 0;
  int cpCount = 0;
  int pos = 0;
  while (pos < normLen) {
    int consumed = 0;
    uint32_t cp = 0;
    if (!utf8_decode(normalized + pos, normLen - pos, &consumed, &cp) || consumed <= 0) {
      consumed = 1;
    }
    pos += consumed;
    cpCount++;
    if (cpCount >= STANDARD_TOKEN_MAX_CODEPOINTS) {
      int tokenStart = emitPositions ? *logicalPosition : start;
      int tokenEnd = emitPositions ? *logicalPosition : end;
      rc = xToken(pCtx, 0, normalized + chunkStart, pos - chunkStart, tokenStart, tokenEnd);
      if (rc != SQLITE_OK) {
        return rc;
      }
      if (emitPositions) {
        (*logicalPosition)++;
      }
      chunkStart = pos;
      cpCount = 0;
    }
  }
  if (chunkStart < normLen) {
    int tokenStart = emitPositions ? *logicalPosition : start;
    int tokenEnd = emitPositions ? *logicalPosition : end;
    rc = xToken(pCtx, 0, normalized + chunkStart, normLen - chunkStart, tokenStart, tokenEnd);
    if (rc == SQLITE_OK && emitPositions) {
      (*logicalPosition)++;
    }
  }
  return rc;
}

static int should_skip_token_start_codepoint(int cls, uint32_t cp) {
  if (cls == 0) {
    return 1;
  }
  if ((cls == 2 && (cp == 0x200D || is_arabic_diacritic(cp) || (cp >= 0x06D6 && cp <= 0x06ED))) || is_combining_mark(cp)) {
    return 1;
  }
  return 0;
}

static int maybe_emit_empty_document_token(
    void *pCtx,
    int flags,
    int emitPositions,
    int start,
    int end,
    const char *folded,
    int foldedLen,
    int *logicalPosition,
    int (*xToken)(void *pCtx, int tflags, const char *pToken, int nToken, int iStart, int iEnd)) {
  if ((flags & FTS5_TOKENIZE_DOCUMENT) == 0) {
    return SQLITE_OK;
  }
  if (!contains_tatweel_codepoint(folded, foldedLen)) {
    return SQLITE_OK;
  }
  int tokenStart = emitPositions ? *logicalPosition : start;
  int tokenEnd = emitPositions ? *logicalPosition : end;
  int rc = xToken(pCtx, 0, EMPTY_TERM_SENTINEL, EMPTY_TERM_SENTINEL_LEN, tokenStart, tokenEnd);
  if (rc != SQLITE_OK) {
    return rc;
  }
  if (emitPositions) {
    (*logicalPosition)++;
  }
  return SQLITE_OK;
}


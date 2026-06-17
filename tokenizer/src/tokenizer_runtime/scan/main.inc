static int arabic_tokenize_scan(
    ArabicTokenizer *tokenizer,
    void *pCtx,
    int flags,
    const char *pText,
    int nText,
    int (*xToken)(void *pCtx, int tflags, const char *pToken, int nToken, int iStart, int iEnd)) {
  int emitPositions = ((flags & ARABIC_TOKENIZE_EMIT_POSITIONS) != 0);
  int logicalPosition = 0;

  int idx = 0;
  while (idx < nText) {
    int consumed = 0;
    uint32_t cp = 0;
    if (!utf8_decode(pText + idx, nText - idx, &consumed, &cp)) {
      idx++;
      continue;
    }
    int cls = token_char_class(cp);
    if (should_skip_token_start_codepoint(cls, cp)) {
      idx += consumed;
      continue;
    }

    int start = idx;
    int tokenIsNumeric = (cls == 2 && is_digit_codepoint(cp));
    int end = scan_token_end(pText, nText, idx, cls, tokenIsNumeric);
    idx = end;

    int rawLen = end - start;
    int bufCap = 0;
    int capRc = ar_utf8_expanded_capacity(rawLen, &bufCap);
    if (capRc != SQLITE_OK) {
      return capRc;
    }
    char *folded = sqlite3_malloc64((sqlite3_uint64)bufCap);
    char *normalized = sqlite3_malloc64((sqlite3_uint64)bufCap);
    if (folded == NULL || normalized == NULL) {
      sqlite3_free(folded);
      sqlite3_free(normalized);
      return SQLITE_NOMEM;
    }

    int foldedLen = transform_token_utf8(pText + start, rawLen, folded, bufCap - 1, fold_codepoint_stage1);
    if (foldedLen <= 0) {
      sqlite3_free(folded);
      sqlite3_free(normalized);
      continue;
    }
    if (is_ascii_underscore_only(folded, foldedLen)) {
      sqlite3_free(folded);
      sqlite3_free(normalized);
      continue;
    }
    folded[foldedLen] = '\0';
    if (is_stopword(tokenizer, folded, foldedLen)) {
      sqlite3_free(folded);
      sqlite3_free(normalized);
      if (emitPositions) {
        logicalPosition++;
      }
      continue;
    }

    int normLen = transform_token_utf8(folded, foldedLen, normalized, bufCap - 1, normalize_codepoint_stage2);
    if (normLen <= 0) {
      int emptyRc = maybe_emit_empty_document_token(
          pCtx,
          flags,
          emitPositions,
          start,
          end,
          folded,
          foldedLen,
          &logicalPosition,
          xToken);
      if (emptyRc != SQLITE_OK) {
        sqlite3_free(folded);
        sqlite3_free(normalized);
        return emptyRc;
      }
      sqlite3_free(folded);
      sqlite3_free(normalized);
      continue;
    }
    normalized[normLen] = '\0';

    if (!is_stem_exclusion(tokenizer, normalized, normLen)) {
      int stemLen = stem_token_arabic_light(normalized, normLen, normalized, bufCap - 1);
      if (stemLen < 0) {
        sqlite3_free(folded);
        sqlite3_free(normalized);
        return SQLITE_NOMEM;
      }
      if (stemLen == 0) {
        sqlite3_free(folded);
        sqlite3_free(normalized);
        continue;
      }
      normLen = stemLen;
      normalized[normLen] = '\0';
    }

    int rc = emit_normalized_token_chunks(
        pCtx,
        emitPositions,
        start,
        end,
        normalized,
        normLen,
        &logicalPosition,
        xToken);
    sqlite3_free(folded);
    sqlite3_free(normalized);
    if (rc != SQLITE_OK) {
      return rc;
    }
  }

  return SQLITE_OK;
}

static int arabic_tokenize(
    Fts5Tokenizer *pTokenizer,
    void *pCtx,
    int flags,
    const char *pText,
    int nText,
    int (*xToken)(void *pCtx, int tflags, const char *pToken, int nToken, int iStart, int iEnd)) {
  ArabicTokenizer *tokenizer = (ArabicTokenizer *)pTokenizer;
  char *expandedText = NULL;
  const char *scanText = pText;
  int scanLen = nText;

  if (tokenizer != NULL && tokenizer->enableHonorificExpansions) {
    int rc = expand_honorific_phrases(pText, nText, &expandedText, &scanLen);
    if (rc != SQLITE_OK) {
      return rc;
    }
    if (expandedText != NULL) {
      scanText = expandedText;
    }
  }

  int rc = arabic_tokenize_scan(tokenizer, pCtx, flags, scanText, scanLen, xToken);
  sqlite3_free(expandedText);
  return rc;
}

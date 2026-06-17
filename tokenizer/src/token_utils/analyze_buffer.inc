static void analyze_terms_buffer_free(AnalyzeTermsBuffer *buffer) {
  if (buffer == NULL) {
    return;
  }
  if (buffer->terms != NULL) {
    for (int i = 0; i < buffer->count; i++) {
      sqlite3_free(buffer->terms[i]);
    }
  }
  sqlite3_free(buffer->terms);
  sqlite3_free(buffer->termLens);
  sqlite3_free(buffer->positions);
  memset(buffer, 0, sizeof(*buffer));
}

static int analyze_terms_buffer_append(AnalyzeTermsBuffer *buffer, const char *token, int nToken, int position) {
  if (buffer->rc != SQLITE_OK) {
    return buffer->rc;
  }
  if (buffer->count >= buffer->cap) {
    int required = 0;
    int sumRc = ar_checked_int_sum(buffer->count, 1, &required);
    if (sumRc != SQLITE_OK) {
      buffer->rc = sumRc;
      return sumRc;
    }
    int nextCap = 0;
    int growRc = ar_grow_capacity(buffer->cap, required, 8, &nextCap);
    if (growRc != SQLITE_OK) {
      buffer->rc = growRc;
      return growRc;
    }
    char **nextTerms = ar_realloc_array(buffer->terms, nextCap, sizeof(char *));
    if (nextTerms == NULL) {
      buffer->rc = SQLITE_NOMEM;
      return SQLITE_NOMEM;
    }
    buffer->terms = nextTerms;
    int *nextLens = ar_realloc_array(buffer->termLens, nextCap, sizeof(int));
    if (nextLens == NULL) {
      buffer->rc = SQLITE_NOMEM;
      return SQLITE_NOMEM;
    }
    buffer->termLens = nextLens;
    int *nextPositions = ar_realloc_array(buffer->positions, nextCap, sizeof(int));
    if (nextPositions == NULL) {
      buffer->rc = SQLITE_NOMEM;
      return SQLITE_NOMEM;
    }
    buffer->positions = nextPositions;
    buffer->cap = nextCap;
  }
  if (nToken < 0 || nToken == INT_MAX) {
    buffer->rc = SQLITE_TOOBIG;
    return SQLITE_TOOBIG;
  }
  char *copy = sqlite3_malloc64((sqlite3_uint64)nToken + 1);
  if (copy == NULL) {
    buffer->rc = SQLITE_NOMEM;
    return SQLITE_NOMEM;
  }
  memcpy(copy, token, (size_t)nToken);
  copy[nToken] = '\0';
  buffer->terms[buffer->count] = copy;
  buffer->termLens[buffer->count] = nToken;
  buffer->positions[buffer->count] = position;
  buffer->count++;
  return SQLITE_OK;
}

static int analyze_collect_token(void *pCtx, int tflags, const char *pToken, int nToken, int iStart, int iEnd) {
  (void)tflags;
  (void)iStart;
  (void)iEnd;
  AnalyzeTermsBuffer *buffer = (AnalyzeTermsBuffer *)pCtx;
  if (nToken == EMPTY_TERM_SENTINEL_LEN && memcmp(pToken, EMPTY_TERM_SENTINEL, (size_t)EMPTY_TERM_SENTINEL_LEN) == 0) {
    return SQLITE_OK;
  }
  return analyze_terms_buffer_append(buffer, pToken, nToken, -1);
}

static int analyze_collect_token_with_position(void *pCtx, int tflags, const char *pToken, int nToken, int iStart, int iEnd) {
  (void)tflags;
  (void)iEnd;
  AnalyzeTermsBuffer *buffer = (AnalyzeTermsBuffer *)pCtx;
  if (nToken == EMPTY_TERM_SENTINEL_LEN && memcmp(pToken, EMPTY_TERM_SENTINEL, (size_t)EMPTY_TERM_SENTINEL_LEN) == 0) {
    return SQLITE_OK;
  }
  return analyze_terms_buffer_append(buffer, pToken, nToken, iStart);
}


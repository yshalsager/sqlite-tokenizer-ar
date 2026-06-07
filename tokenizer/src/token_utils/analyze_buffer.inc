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
  if (buffer->oom) {
    return SQLITE_NOMEM;
  }
  if (buffer->count >= buffer->cap) {
    int nextCap = (buffer->cap == 0) ? 8 : buffer->cap * 2;
    char **nextTerms = sqlite3_realloc(buffer->terms, (size_t)nextCap * sizeof(char *));
    int *nextLens = sqlite3_realloc(buffer->termLens, (size_t)nextCap * sizeof(int));
    int *nextPositions = sqlite3_realloc(buffer->positions, (size_t)nextCap * sizeof(int));
    if (nextTerms == NULL || nextLens == NULL || nextPositions == NULL) {
      if (nextTerms != NULL) {
        buffer->terms = nextTerms;
      }
      if (nextLens != NULL) {
        buffer->termLens = nextLens;
      }
      if (nextPositions != NULL) {
        buffer->positions = nextPositions;
      }
      buffer->oom = 1;
      return SQLITE_NOMEM;
    }
    buffer->terms = nextTerms;
    buffer->termLens = nextLens;
    buffer->positions = nextPositions;
    buffer->cap = nextCap;
  }
  char *copy = sqlite3_malloc((size_t)nToken + 1);
  if (copy == NULL) {
    buffer->oom = 1;
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


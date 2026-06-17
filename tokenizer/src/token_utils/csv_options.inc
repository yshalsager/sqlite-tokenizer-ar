static int normalize_stem_exclusion_term(const char *input, int nInput, char **outTerm, int *outLen) {
  int cap = 0;
  int rc = ar_utf8_expanded_capacity(nInput, &cap);
  if (rc != SQLITE_OK) {
    return rc;
  }

  char *folded = sqlite3_malloc64((sqlite3_uint64)cap);
  char *normalized = sqlite3_malloc64((sqlite3_uint64)cap);
  if (folded == NULL || normalized == NULL) {
    sqlite3_free(folded);
    sqlite3_free(normalized);
    return SQLITE_NOMEM;
  }

  int foldedLen = transform_token_utf8(input, nInput, folded, cap - 1, fold_codepoint_stage1);
  if (foldedLen <= 0) {
    sqlite3_free(folded);
    sqlite3_free(normalized);
    *outTerm = NULL;
    *outLen = 0;
    return SQLITE_OK;
  }
  folded[foldedLen] = '\0';

  int normalizedLen = transform_token_utf8(folded, foldedLen, normalized, cap - 1, normalize_codepoint_stage2);
  sqlite3_free(folded);
  if (normalizedLen <= 0) {
    sqlite3_free(normalized);
    *outTerm = NULL;
    *outLen = 0;
    return SQLITE_OK;
  }

  normalized[normalizedLen] = '\0';
  *outTerm = normalized;
  *outLen = normalizedLen;
  return SQLITE_OK;
}

static int add_stem_exclusion_csv(ArabicTokenizer *tokenizer, const char *csv) {
  const char *p = csv;
  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == ',') {
      p++;
    }
    if (*p == '\0') {
      break;
    }

    const char *start = p;
    while (*p && *p != ',') {
      p++;
    }
    const char *end = p;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
      end--;
    }
    int rawLen = (int)(end - start);
    if (rawLen <= 0) {
      continue;
    }

    char *term = NULL;
    int len = 0;
    int rc = normalize_stem_exclusion_term(start, rawLen, &term, &len);
    if (rc != SQLITE_OK) {
      return rc;
    }
    if (term == NULL || len <= 0) {
      sqlite3_free(term);
      continue;
    }

    int nextCount = 0;
    if (ar_checked_int_sum(tokenizer->stemExclusionCount, 1, &nextCount) != SQLITE_OK) {
      sqlite3_free(term);
      return SQLITE_TOOBIG;
    }
    StemExclusionTerm *next = ar_realloc_array(tokenizer->stemExclusions, nextCount, sizeof(StemExclusionTerm));
    if (next == NULL) {
      sqlite3_free(term);
      return SQLITE_NOMEM;
    }
    tokenizer->stemExclusions = next;
    tokenizer->stemExclusions[tokenizer->stemExclusionCount].term = term;
    tokenizer->stemExclusions[tokenizer->stemExclusionCount].len = len;
    tokenizer->stemExclusionCount = nextCount;
  }

  return SQLITE_OK;
}

static int add_stopwords_csv(ArabicTokenizer *tokenizer, const char *csv) {
  const char *p = csv;
  while (*p) {
    while (*p == ' ' || *p == '\t' || *p == ',') {
      p++;
    }
    if (*p == '\0') {
      break;
    }

    const char *start = p;
    while (*p && *p != ',') {
      p++;
    }
    const char *end = p;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
      end--;
    }
    int len = (int)(end - start);
    if (len <= 0) {
      continue;
    }

    char *term = sqlite3_malloc64((sqlite3_uint64)len + 1);
    if (term == NULL) {
      return SQLITE_NOMEM;
    }
    memcpy(term, start, len);
    term[len] = '\0';

    int nextCount = 0;
    if (ar_checked_int_sum(tokenizer->stopwordCount, 1, &nextCount) != SQLITE_OK) {
      sqlite3_free(term);
      return SQLITE_TOOBIG;
    }
    StopwordTerm *next = ar_realloc_array(tokenizer->stopwords, nextCount, sizeof(StopwordTerm));
    if (next == NULL) {
      sqlite3_free(term);
      return SQLITE_NOMEM;
    }
    tokenizer->stopwords = next;
    tokenizer->stopwords[tokenizer->stopwordCount].term = term;
    tokenizer->stopwords[tokenizer->stopwordCount].len = len;
    tokenizer->stopwordCount = nextCount;
  }

  return SQLITE_OK;
}

static int is_stem_exclusion(const ArabicTokenizer *tokenizer, const char *token, int nToken) {
  for (int i = 0; i < tokenizer->stemExclusionCount; i++) {
    const StemExclusionTerm *term = &tokenizer->stemExclusions[i];
    if (term->len == nToken && memcmp(token, term->term, (size_t)nToken) == 0) {
      return 1;
    }
  }
  return 0;
}

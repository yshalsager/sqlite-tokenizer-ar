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
    int len = (int)(end - start);
    if (len <= 0) {
      continue;
    }

    char *term = sqlite3_malloc((size_t)len + 1);
    if (term == NULL) {
      return SQLITE_NOMEM;
    }
    memcpy(term, start, len);
    term[len] = '\0';

    StemExclusionTerm *next = sqlite3_realloc(
        tokenizer->stemExclusions, (size_t)(tokenizer->stemExclusionCount + 1) * sizeof(StemExclusionTerm));
    if (next == NULL) {
      sqlite3_free(term);
      return SQLITE_NOMEM;
    }
    tokenizer->stemExclusions = next;
    tokenizer->stemExclusions[tokenizer->stemExclusionCount].term = term;
    tokenizer->stemExclusions[tokenizer->stemExclusionCount].len = len;
    tokenizer->stemExclusionCount++;
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

    char *term = sqlite3_malloc((size_t)len + 1);
    if (term == NULL) {
      return SQLITE_NOMEM;
    }
    memcpy(term, start, len);
    term[len] = '\0';

    StopwordTerm *next = sqlite3_realloc(tokenizer->stopwords, (size_t)(tokenizer->stopwordCount + 1) * sizeof(StopwordTerm));
    if (next == NULL) {
      sqlite3_free(term);
      return SQLITE_NOMEM;
    }
    tokenizer->stopwords = next;
    tokenizer->stopwords[tokenizer->stopwordCount].term = term;
    tokenizer->stopwords[tokenizer->stopwordCount].len = len;
    tokenizer->stopwordCount++;
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


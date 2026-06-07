static int arabic_create(void *ctx, const char **argv, int argc, Fts5Tokenizer **ppOut) {
  (void)ctx;

  ArabicTokenizer *tokenizer = sqlite3_malloc(sizeof(*tokenizer));
  if (tokenizer == NULL) {
    return SQLITE_NOMEM;
  }
  memset(tokenizer, 0, sizeof(*tokenizer));

  for (int i = 0; i < argc; i++) {
    const char *arg = argv[i];
    const char *stemPrefix = "stem_exclusion=";
    const char *stopPrefix = "stopwords=";
    size_t stemPrefixLen = strlen(stemPrefix);
    size_t stopPrefixLen = strlen(stopPrefix);
    if (strncmp(arg, stemPrefix, stemPrefixLen) == 0) {
      int rc = add_stem_exclusion_csv(tokenizer, arg + stemPrefixLen);
      if (rc != SQLITE_OK) {
        arabic_free_tokenizer_data(tokenizer);
        sqlite3_free(tokenizer);
        return rc;
      }
    } else if (strcmp(arg, "stem_exclusion") == 0) {
      if (i + 1 >= argc) {
        arabic_free_tokenizer_data(tokenizer);
        sqlite3_free(tokenizer);
        return SQLITE_ERROR;
      }
      int rc = add_stem_exclusion_csv(tokenizer, argv[++i]);
      if (rc != SQLITE_OK) {
        arabic_free_tokenizer_data(tokenizer);
        sqlite3_free(tokenizer);
        return rc;
      }
    } else if (strncmp(arg, stopPrefix, stopPrefixLen) == 0) {
      tokenizer->hasCustomStopwords = 1;
      int rc = add_stopwords_csv(tokenizer, arg + stopPrefixLen);
      if (rc != SQLITE_OK) {
        arabic_free_tokenizer_data(tokenizer);
        sqlite3_free(tokenizer);
        return rc;
      }
    } else if (strcmp(arg, "stopwords") == 0) {
      if (i + 1 >= argc) {
        arabic_free_tokenizer_data(tokenizer);
        sqlite3_free(tokenizer);
        return SQLITE_ERROR;
      }
      tokenizer->hasCustomStopwords = 1;
      int rc = add_stopwords_csv(tokenizer, argv[++i]);
      if (rc != SQLITE_OK) {
        arabic_free_tokenizer_data(tokenizer);
        sqlite3_free(tokenizer);
        return rc;
      }
    } else if (strcmp(arg, "disable_stopwords") == 0) {
      tokenizer->hasCustomStopwords = 1;
    }
  }

  *ppOut = (Fts5Tokenizer *)tokenizer;
  return SQLITE_OK;
}


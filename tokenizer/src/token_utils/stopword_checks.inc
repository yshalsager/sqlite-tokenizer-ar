static int is_stopword(const ArabicTokenizer *tokenizer, const char *token, int nToken) {
  if (tokenizer->hasCustomStopwords) {
    for (int i = 0; i < tokenizer->stopwordCount; i++) {
      const StopwordTerm *stop = &tokenizer->stopwords[i];
      if (stop->len == nToken && memcmp(token, stop->term, (size_t)nToken) == 0) {
        return 1;
      }
    }
    return 0;
  }

  for (int i = 0; i < kArabicStopwords99Count; i++) {
    const char *stop = kArabicStopwords99[i];
    int stopLen = (int)strlen(stop);
    if (stopLen == nToken && memcmp(token, stop, nToken) == 0) {
      return 1;
    }
  }
  return 0;
}


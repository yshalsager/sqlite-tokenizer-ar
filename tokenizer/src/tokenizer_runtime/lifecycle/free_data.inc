static void arabic_free_tokenizer_data(ArabicTokenizer *tokenizer) {
  if (tokenizer == NULL) {
    return;
  }
  for (int i = 0; i < tokenizer->stemExclusionCount; i++) {
    sqlite3_free(tokenizer->stemExclusions[i].term);
  }
  for (int i = 0; i < tokenizer->stopwordCount; i++) {
    sqlite3_free(tokenizer->stopwords[i].term);
  }
  sqlite3_free(tokenizer->stemExclusions);
  sqlite3_free(tokenizer->stopwords);
  tokenizer->stemExclusions = NULL;
  tokenizer->stemExclusionCount = 0;
  tokenizer->stopwords = NULL;
  tokenizer->stopwordCount = 0;
  tokenizer->hasCustomStopwords = 0;
  tokenizer->enableHonorificExpansions = 0;
}

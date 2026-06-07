static void arabic_delete(Fts5Tokenizer *pTokenizer) {
  ArabicTokenizer *tokenizer = (ArabicTokenizer *)pTokenizer;
  arabic_free_tokenizer_data(tokenizer);
  sqlite3_free(pTokenizer);
}


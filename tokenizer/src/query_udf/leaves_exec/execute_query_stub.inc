typedef struct ExecuteQueryBookLen {
  int bookId;
  int docLen;
} ExecuteQueryBookLen;

typedef struct ExecuteQueryHit {
  int fieldKind; /* 1=page, 2=title */
  int bookId;
  int itemId;
  double score;
} ExecuteQueryHit;

typedef struct ExecuteQueryBoostedToken {
  char *term;
  int nTerm;
  float boost;
} ExecuteQueryBoostedToken;

enum {
  EXECUTE_QUERY_COMPILE_OK = 0,
  EXECUTE_QUERY_COMPILE_UNSUPPORTED = 1,
  EXECUTE_QUERY_COMPILE_NOMEM = 2,
};

static int execute_query_append_double(char **json, int *jsonLen, int *jsonCap, double value) {
  char buf[64];
  int wrote = snprintf(buf, sizeof(buf), "%.17g", value);
  if (wrote < 0) {
    return SQLITE_ERROR;
  }
  int n = (int)strlen(buf);
  if (n <= 0) {
    buf[0] = '0';
    n = 1;
  }
  return json_builder_append_bytes(json, jsonLen, jsonCap, buf, n);
}

static int execute_query_contains_true_option(const char *json, int nJson, const char *key) {
  if (json == NULL || nJson <= 0 || key == NULL || key[0] == '\0') {
    return 0;
  }
  char needle[96];
  int nNeedle = sqlite3_snprintf((int)sizeof(needle), needle, "\"%s\"", key) ? (int)strlen(needle) : 0;
  if (nNeedle <= 0 || nNeedle >= (int)sizeof(needle)) {
    return 0;
  }

  for (int i = 0; i + nNeedle < nJson; i++) {
    if (memcmp(json + i, needle, (size_t)nNeedle) != 0) {
      continue;
    }
    int j = i + nNeedle;
    while (j < nJson && isspace((unsigned char)json[j])) {
      j++;
    }
    if (j >= nJson || json[j] != ':') {
      continue;
    }
    j++;
    while (j < nJson && isspace((unsigned char)json[j])) {
      j++;
    }
    if (j + 4 <= nJson && memcmp(json + j, "true", 4) == 0) {
      return 1;
    }
    return 0;
  }
  return 0;
}

static int execute_query_is_dangling_field_scope(const char *query, int nQuery) {
  int start = 0;
  int end = 0;
  trim_ascii_space_span(query, nQuery, &start, &end);
  if (start >= end) {
    return 0;
  }

  int s = start;
  if (query[s] == '+' || query[s] == '-') {
    s++;
    while (s < end && isspace((unsigned char)query[s])) {
      s++;
    }
  }
  if ((end - s) >= 3 && token_equals_ascii_ci(query + s, 3, "NOT") && (s + 3 == end || isspace((unsigned char)query[s + 3]))) {
    s += 3;
    while (s < end && isspace((unsigned char)query[s])) {
      s++;
    }
  }
  if (s >= end) {
    return 0;
  }

  int remaining = end - s;
  if (remaining == 5 && token_equals_ascii_ci(query + s, 5, "page:")) {
    return 1;
  }
  if (remaining == 6 && token_equals_ascii_ci(query + s, 6, "title:")) {
    return 1;
  }
  return 0;
}

static int execute_query_is_ascii_boolean_keyword(const char *token, int nToken) {
  if (token == NULL || nToken <= 0) {
    return 0;
  }
  return token_equals_ascii_ci(token, nToken, "AND") || token_equals_ascii_ci(token, nToken, "OR") ||
         token_equals_ascii_ci(token, nToken, "NOT");
}

static int execute_query_has_unclosed_field_group(const char *query, int nQuery) {
  if (query == NULL || nQuery <= 0) {
    return 0;
  }
  for (int i = 0; i < nQuery; i++) {
    if (query[i] == '\\' && i + 1 < nQuery) {
      i++;
      continue;
    }
    if (i + 5 < nQuery && token_equals_ascii_ci(query + i, 5, "page:") && query[i + 5] == '(') {
      return 1;
    }
    if (i + 6 < nQuery && token_equals_ascii_ci(query + i, 6, "title:") && query[i + 6] == '(') {
      return 1;
    }
  }
  return 0;
}

static int execute_query_is_dangling_boolean_operator(const char *query, int nQuery) {
  int start = 0;
  int end = 0;
  trim_ascii_space_span(query, nQuery, &start, &end);
  if (start >= end) {
    return 0;
  }

  int firstEnd = start;
  while (firstEnd < end && !isspace((unsigned char)query[firstEnd])) {
    firstEnd++;
  }
  int lastStart = end - 1;
  while (lastStart >= start && isspace((unsigned char)query[lastStart])) {
    lastStart--;
  }
  int lastEnd = lastStart + 1;
  while (lastStart >= start && !isspace((unsigned char)query[lastStart])) {
    lastStart--;
  }
  lastStart++;
  if (firstEnd == end && execute_query_is_ascii_boolean_keyword(query + start, end - start)) {
    return 1;
  }
  if (execute_query_is_ascii_boolean_keyword(query + lastStart, lastEnd - lastStart)) {
    return 1;
  }
  return 0;
}

static const char *execute_query_classify_simple_parse_error(const char *query, int nQuery) {
  int start = 0;
  int end = 0;
  trim_ascii_space_span(query, nQuery, &start, &end);
  if (start >= end) {
    return NULL;
  }
  if (execute_query_is_dangling_field_scope(query, nQuery)) {
    return "dangling field scope";
  }

  int inQuote = 0;
  int depth = 0;
  for (int i = start; i < end; i++) {
    char ch = query[i];
    if (ch == '\\' && i + 1 < end) {
      i++;
      continue;
    }
    if (ch == '"') {
      inQuote = !inQuote;
      continue;
    }
    if (inQuote) {
      continue;
    }
    if (ch == '(') {
      depth++;
      continue;
    }
    if (ch == ')') {
      if (depth <= 0) {
        return "unmatched closing parenthesis";
      }
      depth--;
    }
  }
  if (inQuote) {
    return "unclosed quote in query";
  }
  if (depth > 0) {
    if (execute_query_has_unclosed_field_group(query + start, end - start)) {
      return "unclosed field-group parentheses in query";
    }
    return "unclosed group in query";
  }
  if (execute_query_is_dangling_boolean_operator(query + start, end - start)) {
    return "dangling boolean operator";
  }
  return NULL;
}

static int execute_query_field_kind(const char *field, int nField) {
  return parse_boosted_token_clause_runtime_field(field, nField);
}

static const char *execute_query_field_name(int fieldKind) {
  return (fieldKind == 1) ? "page" : "title";
}

static int execute_query_hit_compare_field(const void *left, const void *right) {
  const ExecuteQueryHit *a = (const ExecuteQueryHit *)left;
  const ExecuteQueryHit *b = (const ExecuteQueryHit *)right;
  if (a->score < b->score) {
    return -1;
  }
  if (a->score > b->score) {
    return 1;
  }
  if (a->bookId < b->bookId) {
    return -1;
  }
  if (a->bookId > b->bookId) {
    return 1;
  }
  if (a->itemId < b->itemId) {
    return -1;
  }
  if (a->itemId > b->itemId) {
    return 1;
  }
  return 0;
}

static int execute_query_hit_compare_merged(const void *left, const void *right) {
  const ExecuteQueryHit *a = (const ExecuteQueryHit *)left;
  const ExecuteQueryHit *b = (const ExecuteQueryHit *)right;
  if (a->score < b->score) {
    return -1;
  }
  if (a->score > b->score) {
    return 1;
  }
  if (a->fieldKind < b->fieldKind) {
    return -1;
  }
  if (a->fieldKind > b->fieldKind) {
    return 1;
  }
  if (a->bookId < b->bookId) {
    return -1;
  }
  if (a->bookId > b->bookId) {
    return 1;
  }
  if (a->itemId < b->itemId) {
    return -1;
  }
  if (a->itemId > b->itemId) {
    return 1;
  }
  return 0;
}

static int execute_query_hits_append(ExecuteQueryHit **hits, int *count, int *cap, int fieldKind, int bookId, int itemId, double score) {
  if (*count >= *cap) {
    int required = 0;
    int rc = ar_checked_int_sum(*count, 1, &required);
    if (rc != SQLITE_OK) {
      return rc;
    }
    int nextCap = 0;
    rc = ar_grow_capacity(*cap, required, 16, &nextCap);
    if (rc != SQLITE_OK) {
      return rc;
    }
    ExecuteQueryHit *next = ar_realloc_array(*hits, nextCap, sizeof(ExecuteQueryHit));
    if (next == NULL) {
      return SQLITE_NOMEM;
    }
    *hits = next;
    *cap = nextCap;
  }
  (*hits)[*count].fieldKind = fieldKind;
  (*hits)[*count].bookId = bookId;
  (*hits)[*count].itemId = itemId;
  (*hits)[*count].score = score;
  (*count)++;
  return SQLITE_OK;
}

static int execute_query_lookup_doc_len(const ExecuteQueryBookLen *lens, int lensCount, int bookId) {
  for (int i = 0; i < lensCount; i++) {
    if (lens[i].bookId == bookId) {
      return lens[i].docLen;
    }
  }
  return 0;
}

static int execute_query_book_lens_append(ExecuteQueryBookLen **lens, int *count, int *cap, int bookId, int docLen) {
  if (*count >= *cap) {
    int required = 0;
    int rc = ar_checked_int_sum(*count, 1, &required);
    if (rc != SQLITE_OK) {
      return rc;
    }
    int nextCap = 0;
    rc = ar_grow_capacity(*cap, required, 16, &nextCap);
    if (rc != SQLITE_OK) {
      return rc;
    }
    ExecuteQueryBookLen *next = ar_realloc_array(*lens, nextCap, sizeof(ExecuteQueryBookLen));
    if (next == NULL) {
      return SQLITE_NOMEM;
    }
    *lens = next;
    *cap = nextCap;
  }
  (*lens)[*count].bookId = bookId;
  (*lens)[*count].docLen = docLen;
  (*count)++;
  return SQLITE_OK;
}

static int execute_query_ensure_vocab_tables(sqlite3 *db) {
  const char *sql =
      "CREATE VIRTUAL TABLE IF NOT EXISTS qcv_page_vocab USING fts5vocab(page_fts, 'row');"
      "CREATE VIRTUAL TABLE IF NOT EXISTS qcv_title_vocab USING fts5vocab(title_fts, 'row');"
      "CREATE VIRTUAL TABLE IF NOT EXISTS qcv_page_vocab_inst USING fts5vocab(page_fts, 'instance');"
      "CREATE VIRTUAL TABLE IF NOT EXISTS qcv_title_vocab_inst USING fts5vocab(title_fts, 'instance');";
  char *err = NULL;
  int rc = sqlite3_exec(db, sql, NULL, NULL, &err);
  if (rc != SQLITE_OK) {
    sqlite3_free(err);
    return rc;
  }
  return SQLITE_OK;
}

static int execute_query_lucene_idf_via_sql(sqlite3 *db, int docCount, int docFreq, float *outIdf) {
  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, "SELECT sqlite_tokenizer_ar_lucene_idf(?, ?)", -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = sqlite3_bind_int(stmt, 1, docCount);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 2, docFreq);
  }
  if (rc != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return rc;
  }
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *outIdf = (float)sqlite3_column_double(stmt, 0);
    rc = SQLITE_OK;
  }
  int finalizeRc = sqlite3_finalize(stmt);
  if (rc != SQLITE_OK) {
    return rc;
  }
  if (finalizeRc != SQLITE_OK) {
    return finalizeRc;
  }
  return SQLITE_OK;
}

static int execute_query_load_field_stats(
    sqlite3 *db,
    int fieldKind,
    ExecuteQueryBookLen **lensOut,
    int *lensCountOut,
    int *docCountOut,
    float *avgdlOut) {
  const char *sql = (fieldKind == 1)
                        ? "SELECT m.book_id, COUNT(*) FROM qcv_page_vocab_inst v JOIN page_doc_map m ON m.rowid = v.doc GROUP BY m.book_id"
                        : "SELECT m.book_id, COUNT(*) FROM qcv_title_vocab_inst v JOIN title_doc_map m ON m.rowid = v.doc GROUP BY m.book_id";

  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return rc;
  }

  ExecuteQueryBookLen *lens = NULL;
  int lensCount = 0;
  int lensCap = 0;
  int totalTokens = 0;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    int bookId = sqlite3_column_int(stmt, 0);
    int docLen = sqlite3_column_int(stmt, 1);
    if (docLen < 0) {
      docLen = 0;
    }
    rc = execute_query_book_lens_append(&lens, &lensCount, &lensCap, bookId, docLen);
    if (rc != SQLITE_OK) {
      break;
    }
    totalTokens += docLen;
  }
  int finalizeRc = sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    rc = SQLITE_OK;
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(lens);
    if (finalizeRc != SQLITE_OK) {
      return finalizeRc;
    }
    return rc;
  }
  if (finalizeRc != SQLITE_OK) {
    sqlite3_free(lens);
    return finalizeRc;
  }

  *lensOut = lens;
  *lensCountOut = lensCount;
  *docCountOut = lensCount;
  *avgdlOut = (lensCount > 0) ? ((float)totalTokens / (float)lensCount) : 0.0f;
  return SQLITE_OK;
}

static int execute_query_load_doc_freq(sqlite3 *db, int fieldKind, const char *term, int nTerm, int *docFreqOut) {
  const char *sql = (fieldKind == 1) ? "SELECT doc FROM qcv_page_vocab WHERE term = ?" : "SELECT doc FROM qcv_title_vocab WHERE term = ?";
  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = sqlite3_bind_text(stmt, 1, term, nTerm, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return rc;
  }

  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *docFreqOut = sqlite3_column_int(stmt, 0);
    rc = SQLITE_OK;
  } else if (rc == SQLITE_DONE) {
    *docFreqOut = 0;
    rc = SQLITE_OK;
  }
  int finalizeRc = sqlite3_finalize(stmt);
  if (rc != SQLITE_OK) {
    return rc;
  }
  if (finalizeRc != SQLITE_OK) {
    return finalizeRc;
  }
  return SQLITE_OK;
}

static int execute_query_collect_field_hits(
    sqlite3 *db,
    int fieldKind,
    const char *term,
    int nTerm,
    int limit,
    ExecuteQueryHit **hitsOut,
    int *hitCountOut) {
  *hitsOut = NULL;
  *hitCountOut = 0;
  if (term == NULL || nTerm <= 0 || limit <= 0) {
    return SQLITE_OK;
  }

  ExecuteQueryBookLen *lens = NULL;
  int lensCount = 0;
  int docCount = 0;
  float avgdl = 0.0f;
  int rc = execute_query_load_field_stats(db, fieldKind, &lens, &lensCount, &docCount, &avgdl);
  if (rc != SQLITE_OK) {
    return rc;
  }
  if (docCount <= 0 || avgdl <= 0.0f) {
    sqlite3_free(lens);
    return SQLITE_OK;
  }

  int docFreq = 0;
  rc = execute_query_load_doc_freq(db, fieldKind, term, nTerm, &docFreq);
  if (rc != SQLITE_OK) {
    sqlite3_free(lens);
    return rc;
  }
  if (docFreq <= 0) {
    sqlite3_free(lens);
    return SQLITE_OK;
  }
  float weight = 0.0f;
  rc = execute_query_lucene_idf_via_sql(db, docCount, docFreq, &weight);
  if (rc != SQLITE_OK) {
    sqlite3_free(lens);
    return rc;
  }

  const char *sql = (fieldKind == 1)
                        ? "SELECT m.book_id, m.page_id, COUNT(*) AS tf FROM qcv_page_vocab_inst v JOIN page_doc_map m ON m.rowid = v.doc WHERE v.term = ? GROUP BY m.book_id, m.page_id"
                        : "SELECT m.book_id, m.title_id, COUNT(*) AS tf FROM qcv_title_vocab_inst v JOIN title_doc_map m ON m.rowid = v.doc WHERE v.term = ? GROUP BY m.book_id, m.title_id";
  sqlite3_stmt *stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    sqlite3_free(lens);
    return rc;
  }
  rc = sqlite3_bind_text(stmt, 1, term, nTerm, SQLITE_TRANSIENT);
  if (rc != SQLITE_OK) {
    sqlite3_finalize(stmt);
    sqlite3_free(lens);
    return rc;
  }

  sqlite3_stmt *scoreStmt = NULL;
  rc = sqlite3_prepare_v2(db, "SELECT sqlite_tokenizer_ar_lucene_term_score(?, ?, ?, ?, ?, ?)", -1, &scoreStmt, NULL);
  if (rc != SQLITE_OK) {
    sqlite3_finalize(stmt);
    sqlite3_free(lens);
    return rc;
  }

  ExecuteQueryHit *hits = NULL;
  int hitCount = 0;
  int hitCap = 0;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    int bookId = sqlite3_column_int(stmt, 0);
    int itemId = sqlite3_column_int(stmt, 1);
    float tf = (float)sqlite3_column_double(stmt, 2);
    int rawDocLen = execute_query_lookup_doc_len(lens, lensCount, bookId);
    rc = sqlite3_bind_double(scoreStmt, 1, (double)weight);
    if (rc == SQLITE_OK) {
      rc = sqlite3_bind_double(scoreStmt, 2, (double)tf);
    }
    if (rc == SQLITE_OK) {
      rc = sqlite3_bind_int(scoreStmt, 3, rawDocLen);
    }
    if (rc == SQLITE_OK) {
      rc = sqlite3_bind_double(scoreStmt, 4, 1.2);
    }
    if (rc == SQLITE_OK) {
      rc = sqlite3_bind_double(scoreStmt, 5, 0.75);
    }
    if (rc == SQLITE_OK) {
      rc = sqlite3_bind_double(scoreStmt, 6, (double)avgdl);
    }
    if (rc != SQLITE_OK) {
      break;
    }
    rc = sqlite3_step(scoreStmt);
    if (rc != SQLITE_ROW) {
      break;
    }
    float score = -(float)sqlite3_column_double(scoreStmt, 0);
    sqlite3_reset(scoreStmt);
    sqlite3_clear_bindings(scoreStmt);
    rc = execute_query_hits_append(&hits, &hitCount, &hitCap, fieldKind, bookId, itemId, score);
    if (rc != SQLITE_OK) {
      break;
    }
  }
  int finalizeRc = sqlite3_finalize(stmt);
  int scoreFinalizeRc = sqlite3_finalize(scoreStmt);
  sqlite3_free(lens);
  if (rc == SQLITE_DONE) {
    rc = SQLITE_OK;
  }
  if (rc == SQLITE_ROW) {
    rc = SQLITE_OK;
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(hits);
    if (finalizeRc != SQLITE_OK) {
      return finalizeRc;
    }
    if (scoreFinalizeRc != SQLITE_OK) {
      return scoreFinalizeRc;
    }
    return rc;
  }
  if (finalizeRc != SQLITE_OK) {
    sqlite3_free(hits);
    return finalizeRc;
  }
  if (scoreFinalizeRc != SQLITE_OK) {
    sqlite3_free(hits);
    return scoreFinalizeRc;
  }

  if (hitCount > 1) {
    qsort(hits, (size_t)hitCount, sizeof(ExecuteQueryHit), execute_query_hit_compare_field);
  }
  if (hitCount > limit) {
    hitCount = limit;
  }
  *hitsOut = hits;
  *hitCountOut = hitCount;
  return SQLITE_OK;
}

static int execute_query_analyze_single_term(const char *raw, int nRaw, char **termOut, int *nTermOut, int *tokenCountOut) {
  *termOut = NULL;
  *nTermOut = 0;
  *tokenCountOut = 0;
  if (raw == NULL || nRaw <= 0) {
    return SQLITE_OK;
  }

  ArabicTokenizer tokenizer;
  memset(&tokenizer, 0, sizeof(tokenizer));
  AnalyzeTermsBuffer terms;
  memset(&terms, 0, sizeof(terms));
  int rc = arabic_tokenize((Fts5Tokenizer *)&tokenizer, &terms, FTS5_TOKENIZE_QUERY, raw, nRaw, analyze_collect_token);
  if (rc == SQLITE_OK) {
    rc = terms.rc;
  }
  if (rc != SQLITE_OK) {
    analyze_terms_buffer_free(&terms);
    return rc;
  }
  *tokenCountOut = terms.count;
  if (terms.count == 1) {
    int copyCap = 0;
    if (ar_checked_int_sum(terms.termLens[0], 1, &copyCap) != SQLITE_OK) {
      analyze_terms_buffer_free(&terms);
      return SQLITE_TOOBIG;
    }
    char *copy = sqlite3_malloc64((sqlite3_uint64)copyCap);
    if (copy == NULL) {
      analyze_terms_buffer_free(&terms);
      return SQLITE_NOMEM;
    }
    memcpy(copy, terms.terms[0], (size_t)terms.termLens[0]);
    copy[terms.termLens[0]] = '\0';
    *termOut = copy;
    *nTermOut = terms.termLens[0];
  }
  analyze_terms_buffer_free(&terms);
  return SQLITE_OK;
}

static int execute_query_append_match_term(char **json, int *jsonLen, int *jsonCap, const char *token, int nToken) {
  int rc = json_builder_append_char(json, jsonLen, jsonCap, '"');
  for (int i = 0; rc == SQLITE_OK && i < nToken; i++) {
    char ch = token[i];
    if (ch == '"') {
      rc = json_builder_append_bytes(json, jsonLen, jsonCap, "\"\"", 2);
    } else {
      rc = json_builder_append_char(json, jsonLen, jsonCap, ch);
    }
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(json, jsonLen, jsonCap, '"');
  }
  return rc;
}

static int execute_query_build_payload(
    int requestedFieldKind,
    const char *pageCompiled,
    int nPageCompiled,
    const char *titleCompiled,
    int nTitleCompiled,
    const ExecuteQueryHit *hits,
    int hitCount,
    int scoresAreF32,
    char **jsonOut,
    int *jsonLenOut) {
  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "{\"compiled\":{", 13);
  if (rc == SQLITE_OK) {
    if (requestedFieldKind == 1) {
      rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "\"page\":", 7);
      if (rc == SQLITE_OK) {
        rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, pageCompiled, nPageCompiled);
      }
    } else if (requestedFieldKind == 2) {
      rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "\"title\":", 8);
      if (rc == SQLITE_OK) {
        rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, titleCompiled, nTitleCompiled);
      }
    } else {
      rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "\"page\":", 7);
      if (rc == SQLITE_OK) {
        rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, pageCompiled, nPageCompiled);
      }
      if (rc == SQLITE_OK) {
        rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"title\":", 9);
      }
      if (rc == SQLITE_OK) {
        rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, titleCompiled, nTitleCompiled);
      }
    }
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "},\"hits\":[", 10);
  }
  for (int i = 0; rc == SQLITE_OK && i < hitCount; i++) {
    if (i > 0) {
      rc = json_builder_append_char(&json, &jsonLen, &jsonCap, ',');
      if (rc != SQLITE_OK) {
        break;
      }
    }
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "{\"field\":", 9);
    if (rc == SQLITE_OK) {
      const char *fieldName = execute_query_field_name(hits[i].fieldKind);
      rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, fieldName, (int)strlen(fieldName));
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"book_id\":", 11);
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_int(&json, &jsonLen, &jsonCap, hits[i].bookId);
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"item_id\":", 11);
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_int(&json, &jsonLen, &jsonCap, hits[i].itemId);
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"score\":", 9);
    }
    if (rc == SQLITE_OK) {
      rc = execute_query_append_double(&json, &jsonLen, &jsonCap, (double)hits[i].score);
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_char(&json, &jsonLen, &jsonCap, '}');
    }
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "],\"scores_are_f32\":", 19);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, scoresAreF32 ? "true" : "false", scoresAreF32 ? 4 : 5);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&json, &jsonLen, &jsonCap, '}');
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    return rc;
  }
  json[jsonLen] = '\0';
  *jsonOut = json;
  *jsonLenOut = jsonLen;
  return SQLITE_OK;
}

static int execute_query_build_nohit_payload(int requestedFieldKind, char **jsonOut, int *jsonLenOut) {
  return execute_query_build_payload(
      requestedFieldKind,
      "\"__nohit__\"",
      11,
      "\"__nohit__\"",
      11,
      NULL,
      0,
      1,
      jsonOut,
      jsonLenOut);
}

typedef struct ExecuteParsedTokenClause {
  int scopeFieldKind; /* 0=none, 1=page, 2=title */
  const char *baseToken;
  int nBaseToken;
  float boost;
  char *analyzedTerm;
  int nAnalyzedTerm;
  int analyzedCount;
} ExecuteParsedTokenClause;

typedef struct ExecutePatternTokenClause {
  int scopeFieldKind; /* 0=none, 1=page, 2=title */
  const char *baseToken;
  int nBaseToken;
  float boost;
  int hasWildcard;
  int isFuzzy;
  const char *fuzzyBase;
  int nFuzzyBase;
  int fuzzyEdits;
} ExecutePatternTokenClause;

static float execute_query_f32_mul(float left, float right) {
  return (float)(left * right);
}

static float execute_query_f32_add(float left, float right) {
  return (float)(left + right);
}

static void execute_query_free_parsed_token_clause(ExecuteParsedTokenClause *clause) {
  if (clause == NULL) {
    return;
  }
  sqlite3_free(clause->analyzedTerm);
  memset(clause, 0, sizeof(*clause));
}

static int execute_query_parse_token_clause(const ParsedSimpleAstClause *clause, ExecuteParsedTokenClause *out) {
  memset(out, 0, sizeof(*out));
  out->boost = 1.0f;
  if (!token_equals_ascii_ci(clause->kind, clause->nKind, "token")) {
    return 1;
  }

  const char *scope = NULL;
  int nScope = 0;
  const char *rawToken = clause->value;
  int nRawToken = clause->nValue;
  parse_boosted_token_clause_scope(clause->value, clause->nValue, &scope, &nScope, &rawToken, &nRawToken);
  if (scope != NULL) {
    int scopeKind = execute_query_field_kind(scope, nScope);
    if (scopeKind == 0) {
      return 1;
    }
    out->scopeFieldKind = scopeKind;
  }

  const char *baseToken = rawToken;
  int nBaseToken = nRawToken;
  char boostBuf[64];
  int nBoost = 0;
  double boostValue = 1.0;
  int stripRc = parse_boosted_token_clause_strip_boost(
      rawToken, nRawToken, &baseToken, &nBaseToken, boostBuf, (int)sizeof(boostBuf), &nBoost, &boostValue);
  if (stripRc < 0) {
    return 2;
  }
  if (nBaseToken <= 0 || parse_boosted_token_clause_has_unescaped_wildcard(baseToken, nBaseToken) ||
      parse_boosted_token_clause_is_fuzzy(baseToken, nBaseToken)) {
    return 1;
  }

  out->baseToken = baseToken;
  out->nBaseToken = nBaseToken;
  out->boost = (stripRc > 0) ? (float)boostValue : 1.0f;

  int rc = execute_query_analyze_single_term(
      baseToken, nBaseToken, &out->analyzedTerm, &out->nAnalyzedTerm, &out->analyzedCount);
  if (rc != SQLITE_OK) {
    execute_query_free_parsed_token_clause(out);
    return 3;
  }
  return 0;
}

typedef struct ExecuteCompiledTokenState {
  const char *token;
  int nToken;
  int isStopwordNoHit;
} ExecuteCompiledTokenState;

static void execute_query_resolve_compiled_token_state_for_field(
    const ExecuteParsedTokenClause *clause,
    int runtimeFieldKind,
    ExecuteCompiledTokenState *out) {
  if (out == NULL) {
    return;
  }
  out->token = "__nohit__";
  out->nToken = 9;
  out->isStopwordNoHit = 0;
  if (clause == NULL) {
    return;
  }
  if (runtimeFieldKind != 0 && clause->scopeFieldKind != 0 && clause->scopeFieldKind != runtimeFieldKind) {
    return;
  }
  if (clause->analyzedCount > 0 && clause->baseToken != NULL && clause->nBaseToken > 0) {
    out->token = clause->baseToken;
    out->nToken = clause->nBaseToken;
    return;
  }
  out->isStopwordNoHit = 1;
}

static int execute_query_parse_pattern_token_clause(const ParsedSimpleAstClause *clause, ExecutePatternTokenClause *out) {
  memset(out, 0, sizeof(*out));
  out->boost = 1.0f;
  if (!token_equals_ascii_ci(clause->kind, clause->nKind, "token")) {
    return 1;
  }

  const char *scope = NULL;
  int nScope = 0;
  const char *rawToken = clause->value;
  int nRawToken = clause->nValue;
  parse_boosted_token_clause_scope(clause->value, clause->nValue, &scope, &nScope, &rawToken, &nRawToken);
  if (scope != NULL) {
    int scopeKind = execute_query_field_kind(scope, nScope);
    if (scopeKind == 0) {
      return 1;
    }
    out->scopeFieldKind = scopeKind;
  }

  const char *baseToken = rawToken;
  int nBaseToken = nRawToken;
  char boostBuf[64];
  int nBoost = 0;
  double boostValue = 1.0;
  int stripRc = parse_boosted_token_clause_strip_boost(
      rawToken, nRawToken, &baseToken, &nBaseToken, boostBuf, (int)sizeof(boostBuf), &nBoost, &boostValue);
  if (stripRc < 0) {
    return 2;
  }
  if (nBaseToken <= 0) {
    return 1;
  }

  out->baseToken = baseToken;
  out->nBaseToken = nBaseToken;
  out->boost = (stripRc > 0) ? (float)boostValue : 1.0f;
  out->hasWildcard = parse_boosted_token_clause_has_unescaped_wildcard(baseToken, nBaseToken);
  out->isFuzzy = parse_fuzzy_token_span(baseToken, nBaseToken, &out->fuzzyBase, &out->nFuzzyBase, &out->fuzzyEdits);
  if (!out->hasWildcard && !out->isFuzzy) {
    return 1;
  }
  if (out->hasWildcard && out->isFuzzy) {
    return 1;
  }
  return 0;
}

static void execute_query_free_expanded_terms(char **terms, int termCount) {
  if (terms == NULL) {
    return;
  }
  for (int i = 0; i < termCount; i++) {
    sqlite3_free(terms[i]);
  }
  sqlite3_free(terms);
}

static int execute_query_append_expanded_term(char ***termsOut, int *termCountOut, int *termCapOut, const char *term, int nTerm) {
  if (*termCountOut >= *termCapOut) {
    int required = 0;
    int rc = ar_checked_int_sum(*termCountOut, 1, &required);
    if (rc != SQLITE_OK) {
      return rc;
    }
    int nextCap = 0;
    rc = ar_grow_capacity(*termCapOut, required, 16, &nextCap);
    if (rc != SQLITE_OK) {
      return rc;
    }
    char **nextTerms = ar_realloc_array(*termsOut, nextCap, sizeof(char *));
    if (nextTerms == NULL) {
      return SQLITE_NOMEM;
    }
    *termsOut = nextTerms;
    *termCapOut = nextCap;
  }
  int copyCap = 0;
  if (ar_checked_int_sum(nTerm, 1, &copyCap) != SQLITE_OK) {
    return SQLITE_TOOBIG;
  }
  char *copy = sqlite3_malloc64((sqlite3_uint64)copyCap);
  if (copy == NULL) {
    return SQLITE_NOMEM;
  }
  memcpy(copy, term, (size_t)nTerm);
  copy[nTerm] = '\0';
  (*termsOut)[*termCountOut] = copy;
  (*termCountOut)++;
  return SQLITE_OK;
}

static int execute_query_expand_wildcard_terms(
    sqlite3 *db,
    int fieldKind,
    const char *pattern,
    int nPattern,
    int maxExpansions,
    char ***termsOut,
    int *termCountOut) {
  const char *sql = (fieldKind == 1) ? "SELECT term FROM qcv_page_vocab WHERE sqlite_tokenizer_ar_wildcard_match(term, ?) ORDER BY term LIMIT ?"
                                      : "SELECT term FROM qcv_title_vocab WHERE sqlite_tokenizer_ar_wildcard_match(term, ?) ORDER BY term LIMIT ?";
  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = sqlite3_bind_text(stmt, 1, pattern, nPattern, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 2, maxExpansions);
  }
  if (rc != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return rc;
  }

  char **terms = NULL;
  int termCount = 0;
  int termCap = 0;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    const unsigned char *termText = sqlite3_column_text(stmt, 0);
    int nTermText = sqlite3_column_bytes(stmt, 0);
    if (termText == NULL || nTermText <= 0) {
      continue;
    }
    rc = execute_query_append_expanded_term(&terms, &termCount, &termCap, (const char *)termText, nTermText);
    if (rc != SQLITE_OK) {
      break;
    }
  }
  int finalizeRc = sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    rc = SQLITE_OK;
  }
  if (rc != SQLITE_OK) {
    execute_query_free_expanded_terms(terms, termCount);
    if (finalizeRc != SQLITE_OK) {
      return finalizeRc;
    }
    return rc;
  }
  if (finalizeRc != SQLITE_OK) {
    execute_query_free_expanded_terms(terms, termCount);
    return finalizeRc;
  }
  *termsOut = terms;
  *termCountOut = termCount;
  return SQLITE_OK;
}

static int execute_query_expand_fuzzy_terms(
    sqlite3 *db,
    int fieldKind,
    const char *base,
    int nBase,
    int edits,
    int maxExpansions,
    char ***termsOut,
    int *termCountOut) {
  char *normalizedBase = NULL;
  int nNormalizedBase = 0;
  int analyzedCount = 0;
  int rc = execute_query_analyze_single_term(base, nBase, &normalizedBase, &nNormalizedBase, &analyzedCount);
  if (rc != SQLITE_OK) {
    return rc;
  }
  if (analyzedCount <= 0 || normalizedBase == NULL || nNormalizedBase <= 0) {
    sqlite3_free(normalizedBase);
    *termsOut = NULL;
    *termCountOut = 0;
    return SQLITE_OK;
  }

  const char *sql = (fieldKind == 1)
                        ? "SELECT term FROM qcv_page_vocab WHERE sqlite_tokenizer_ar_levenshtein(term, ?, ?) <= ? ORDER BY term LIMIT ?"
                        : "SELECT term FROM qcv_title_vocab WHERE sqlite_tokenizer_ar_levenshtein(term, ?, ?) <= ? ORDER BY term LIMIT ?";
  sqlite3_stmt *stmt = NULL;
  rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    sqlite3_free(normalizedBase);
    return rc;
  }
  rc = sqlite3_bind_text(stmt, 1, normalizedBase, nNormalizedBase, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 2, edits);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 3, edits);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 4, maxExpansions);
  }
  sqlite3_free(normalizedBase);
  if (rc != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return rc;
  }

  char **terms = NULL;
  int termCount = 0;
  int termCap = 0;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    const unsigned char *termText = sqlite3_column_text(stmt, 0);
    int nTermText = sqlite3_column_bytes(stmt, 0);
    if (termText == NULL || nTermText <= 0) {
      continue;
    }
    rc = execute_query_append_expanded_term(&terms, &termCount, &termCap, (const char *)termText, nTermText);
    if (rc != SQLITE_OK) {
      break;
    }
  }
  int finalizeRc = sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    rc = SQLITE_OK;
  }
  if (rc != SQLITE_OK) {
    execute_query_free_expanded_terms(terms, termCount);
    if (finalizeRc != SQLITE_OK) {
      return finalizeRc;
    }
    return rc;
  }
  if (finalizeRc != SQLITE_OK) {
    execute_query_free_expanded_terms(terms, termCount);
    return finalizeRc;
  }
  *termsOut = terms;
  *termCountOut = termCount;
  return SQLITE_OK;
}

static int execute_query_build_single_token_compiled(const char *token, int nToken, char **compiledOut, int *nCompiledOut);
static int execute_query_build_unwrapped_single_term_expression(const char *term, int nTerm, char **compiledOut, int *nCompiledOut);
static int execute_query_wrap_compiled_expression(const char *expr, int nExpr, char **compiledOut, int *nCompiledOut);

static int execute_query_build_pattern_compiled_from_terms(char **terms, int termCount, char **compiledOut, int *nCompiledOut) {
  if (termCount <= 0 || terms == NULL) {
    return execute_query_build_single_token_compiled("__nohit__", 9, compiledOut, nCompiledOut);
  }
  if (termCount == 1) {
    return execute_query_build_single_token_compiled(terms[0], (int)strlen(terms[0]), compiledOut, nCompiledOut);
  }

  char *inner = NULL;
  int nInner = 0;
  int innerCap = 0;
  int rc = json_builder_append_char(&inner, &nInner, &innerCap, '(');
  for (int i = 0; rc == SQLITE_OK && i < termCount; i++) {
    if (i > 0) {
      rc = json_builder_append_bytes(&inner, &nInner, &innerCap, " OR ", 4);
      if (rc != SQLITE_OK) {
        break;
      }
    }
    rc = execute_query_append_match_term(&inner, &nInner, &innerCap, terms[i], (int)strlen(terms[i]));
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&inner, &nInner, &innerCap, ')');
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(inner);
    return rc;
  }
  inner[nInner] = '\0';
  rc = execute_query_wrap_compiled_expression(inner, nInner, compiledOut, nCompiledOut);
  sqlite3_free(inner);
  return rc;
}

static int execute_query_collect_constant_hits(
    sqlite3 *db,
    int fieldKind,
    const char *matchExpr,
    int nMatchExpr,
    int limit,
    ExecuteQueryHit **hitsOut,
    int *hitCountOut) {
  const char *sql = (fieldKind == 1)
                        ? "SELECT m.book_id, m.page_id FROM page_fts JOIN page_doc_map m ON m.rowid = page_fts.rowid "
                          "WHERE page_fts MATCH ? ORDER BY m.book_id, m.page_id LIMIT ?"
                        : "SELECT m.book_id, m.title_id FROM title_fts JOIN title_doc_map m ON m.rowid = title_fts.rowid "
                          "WHERE title_fts MATCH ? ORDER BY m.book_id, m.title_id LIMIT ?";
  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = sqlite3_bind_text(stmt, 1, matchExpr, nMatchExpr, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 2, limit);
  }
  if (rc != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return rc;
  }

  ExecuteQueryHit *hits = NULL;
  int hitCount = 0;
  int hitCap = 0;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    int bookId = sqlite3_column_int(stmt, 0);
    int itemId = sqlite3_column_int(stmt, 1);
    rc = execute_query_hits_append(&hits, &hitCount, &hitCap, fieldKind, bookId, itemId, -1.0f);
    if (rc != SQLITE_OK) {
      break;
    }
  }
  int finalizeRc = sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    rc = SQLITE_OK;
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(hits);
    if (finalizeRc != SQLITE_OK) {
      return finalizeRc;
    }
    return rc;
  }
  if (finalizeRc != SQLITE_OK) {
    sqlite3_free(hits);
    return finalizeRc;
  }
  *hitsOut = hits;
  *hitCountOut = hitCount;
  return SQLITE_OK;
}

static void execute_query_apply_boost_inplace(ExecuteQueryHit *hits, int hitCount, float boost) {
  if (hits == NULL || hitCount <= 0) {
    return;
  }
  if (fabs((double)(boost - 1.0f)) < 1e-12) {
    return;
  }
  for (int i = 0; i < hitCount; i++) {
    hits[i].score = execute_query_f32_mul(hits[i].score, boost);
  }
  if (hitCount > 1) {
    qsort(hits, (size_t)hitCount, sizeof(ExecuteQueryHit), execute_query_hit_compare_field);
  }
}

static int execute_query_find_hit_index(const ExecuteQueryHit *hits, int hitCount, int bookId, int itemId) {
  for (int i = 0; i < hitCount; i++) {
    if (hits[i].bookId == bookId && hits[i].itemId == itemId) {
      return i;
    }
  }
  return -1;
}

#define EXECUTE_QUERY_BOOLEAN_TERM_LIMIT 1000000

static int execute_query_score_simple_boolean_field(
    sqlite3 *db,
    int fieldKind,
    const ExecuteParsedTokenClause *left,
    const ExecuteParsedTokenClause *right,
    int opAnd,
    int limit,
    ExecuteQueryHit **hitsOut,
    int *hitCountOut) {
  *hitsOut = NULL;
  *hitCountOut = 0;

  int leftValid = (left->analyzedCount == 1 && left->analyzedTerm != NULL && left->nAnalyzedTerm > 0);
  int rightValid = (right->analyzedCount == 1 && right->analyzedTerm != NULL && right->nAnalyzedTerm > 0);
  if (!leftValid && !rightValid) {
    return SQLITE_OK;
  }

  if (!leftValid) {
    int rc = execute_query_collect_field_hits(
        db, fieldKind, right->analyzedTerm, right->nAnalyzedTerm, limit, hitsOut, hitCountOut);
    if (rc != SQLITE_OK) {
      return rc;
    }
    execute_query_apply_boost_inplace(*hitsOut, *hitCountOut, right->boost);
    return SQLITE_OK;
  }
  if (!rightValid) {
    int rc = execute_query_collect_field_hits(
        db, fieldKind, left->analyzedTerm, left->nAnalyzedTerm, limit, hitsOut, hitCountOut);
    if (rc != SQLITE_OK) {
      return rc;
    }
    execute_query_apply_boost_inplace(*hitsOut, *hitCountOut, left->boost);
    return SQLITE_OK;
  }

  ExecuteQueryHit *leftHits = NULL;
  int leftCount = 0;
  ExecuteQueryHit *rightHits = NULL;
  int rightCount = 0;
  int rc = execute_query_collect_field_hits(
      db,
      fieldKind,
      left->analyzedTerm,
      left->nAnalyzedTerm,
      EXECUTE_QUERY_BOOLEAN_TERM_LIMIT,
      &leftHits,
      &leftCount);
  if (rc != SQLITE_OK) {
    sqlite3_free(leftHits);
    return rc;
  }
  rc = execute_query_collect_field_hits(
      db,
      fieldKind,
      right->analyzedTerm,
      right->nAnalyzedTerm,
      EXECUTE_QUERY_BOOLEAN_TERM_LIMIT,
      &rightHits,
      &rightCount);
  if (rc != SQLITE_OK) {
    sqlite3_free(leftHits);
    sqlite3_free(rightHits);
    return rc;
  }

  ExecuteQueryHit *merged = NULL;
  int mergedCount = 0;
  int mergedCap = 0;
  if (opAnd) {
    for (int i = 0; i < leftCount; i++) {
      int rightIdx = execute_query_find_hit_index(rightHits, rightCount, leftHits[i].bookId, leftHits[i].itemId);
      if (rightIdx < 0) {
        continue;
      }
      float leftComponent = execute_query_f32_mul(leftHits[i].score, left->boost);
      float rightComponent = execute_query_f32_mul(rightHits[rightIdx].score, right->boost);
      float score = execute_query_f32_add(leftComponent, rightComponent);
      rc = execute_query_hits_append(
          &merged, &mergedCount, &mergedCap, fieldKind, leftHits[i].bookId, leftHits[i].itemId, score);
      if (rc != SQLITE_OK) {
        break;
      }
    }
  } else {
    for (int i = 0; i < leftCount; i++) {
      float score = execute_query_f32_mul(leftHits[i].score, left->boost);
      rc = execute_query_hits_append(
          &merged, &mergedCount, &mergedCap, fieldKind, leftHits[i].bookId, leftHits[i].itemId, score);
      if (rc != SQLITE_OK) {
        break;
      }
    }
    for (int i = 0; rc == SQLITE_OK && i < rightCount; i++) {
      float rightComponent = execute_query_f32_mul(rightHits[i].score, right->boost);
      int idx = execute_query_find_hit_index(merged, mergedCount, rightHits[i].bookId, rightHits[i].itemId);
      if (idx >= 0) {
        merged[idx].score = execute_query_f32_add(merged[idx].score, rightComponent);
        continue;
      }
      rc = execute_query_hits_append(
          &merged, &mergedCount, &mergedCap, fieldKind, rightHits[i].bookId, rightHits[i].itemId, rightComponent);
    }
  }
  sqlite3_free(leftHits);
  sqlite3_free(rightHits);
  if (rc != SQLITE_OK) {
    sqlite3_free(merged);
    return rc;
  }

  if (mergedCount > 1) {
    qsort(merged, (size_t)mergedCount, sizeof(ExecuteQueryHit), execute_query_hit_compare_field);
  }
  if (mergedCount > limit) {
    mergedCount = limit;
  }
  *hitsOut = merged;
  *hitCountOut = mergedCount;
  return SQLITE_OK;
}

static int execute_query_build_single_token_compiled(const char *token, int nToken, char **compiledOut, int *nCompiledOut) {
  char *compiled = NULL;
  int nCompiled = 0;
  int compiledCap = 0;
  int rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, '(');
  if (rc == SQLITE_OK) {
    rc = execute_query_append_match_term(&compiled, &nCompiled, &compiledCap, token, nToken);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, ')');
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(compiled);
    return rc;
  }
  compiled[nCompiled] = '\0';
  *compiledOut = compiled;
  *nCompiledOut = nCompiled;
  return SQLITE_OK;
}

static int execute_query_build_simple_boolean_compiled(
    const char *leftToken,
    int nLeftToken,
    int opAnd,
    const char *rightToken,
    int nRightToken,
    char **compiledOut,
    int *nCompiledOut) {
  char *compiled = NULL;
  int nCompiled = 0;
  int compiledCap = 0;
  int rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, '(');
  if (rc == SQLITE_OK) {
    rc = execute_query_append_match_term(&compiled, &nCompiled, &compiledCap, leftToken, nLeftToken);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, opAnd ? ") AND (" : ") OR (", opAnd ? 7 : 6);
  }
  if (rc == SQLITE_OK) {
    rc = execute_query_append_match_term(&compiled, &nCompiled, &compiledCap, rightToken, nRightToken);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, ')');
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(compiled);
    return rc;
  }
  compiled[nCompiled] = '\0';
  *compiledOut = compiled;
  *nCompiledOut = nCompiled;
  return SQLITE_OK;
}

static int execute_query_build_simple_not_boolean_compiled(
    const char *positiveToken,
    int nPositiveToken,
    const char *negativeToken,
    int nNegativeToken,
    char **compiledOut,
    int *nCompiledOut) {
  char *compiled = NULL;
  int nCompiled = 0;
  int compiledCap = 0;
  int rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, "((", 2);
  if (rc == SQLITE_OK) {
    rc = execute_query_append_match_term(&compiled, &nCompiled, &compiledCap, positiveToken, nPositiveToken);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, ")) NOT ((", 9);
  }
  if (rc == SQLITE_OK) {
    rc = execute_query_append_match_term(&compiled, &nCompiled, &compiledCap, negativeToken, nNegativeToken);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, "))", 2);
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(compiled);
    return rc;
  }
  compiled[nCompiled] = '\0';
  *compiledOut = compiled;
  *nCompiledOut = nCompiled;
  return SQLITE_OK;
}

static int execute_query_build_simple_token_compiled_for_field(
    const ExecuteParsedTokenClause *tokenClause,
    int runtimeFieldKind,
    char **compiledOut,
    int *nCompiledOut) {
  ExecuteCompiledTokenState tokenState;
  memset(&tokenState, 0, sizeof(tokenState));
  execute_query_resolve_compiled_token_state_for_field(tokenClause, runtimeFieldKind, &tokenState);
  if (tokenState.isStopwordNoHit) {
    return execute_query_build_unwrapped_single_term_expression("__nohit__", 9, compiledOut, nCompiledOut);
  }
  return execute_query_build_single_token_compiled(tokenState.token, tokenState.nToken, compiledOut, nCompiledOut);
}

static int execute_query_build_simple_boolean_compiled_for_field(
    const ExecuteParsedTokenClause *leftClause,
    const ExecuteParsedTokenClause *rightClause,
    int opAnd,
    int modeNot,
    int runtimeFieldKind,
    char **compiledOut,
    int *nCompiledOut) {
  ExecuteCompiledTokenState leftState;
  ExecuteCompiledTokenState rightState;
  memset(&leftState, 0, sizeof(leftState));
  memset(&rightState, 0, sizeof(rightState));
  execute_query_resolve_compiled_token_state_for_field(leftClause, runtimeFieldKind, &leftState);
  execute_query_resolve_compiled_token_state_for_field(rightClause, runtimeFieldKind, &rightState);

  if (modeNot) {
    if (leftState.isStopwordNoHit) {
      return execute_query_build_unwrapped_single_term_expression("__nohit__", 9, compiledOut, nCompiledOut);
    }
    if (rightState.isStopwordNoHit) {
      return execute_query_build_single_token_compiled(leftState.token, leftState.nToken, compiledOut, nCompiledOut);
    }
    return execute_query_build_simple_not_boolean_compiled(
        leftState.token, leftState.nToken, rightState.token, rightState.nToken, compiledOut, nCompiledOut);
  }

  if (leftState.isStopwordNoHit && rightState.isStopwordNoHit) {
    return execute_query_build_unwrapped_single_term_expression("__nohit__", 9, compiledOut, nCompiledOut);
  }
  if (leftState.isStopwordNoHit) {
    return execute_query_build_single_token_compiled(rightState.token, rightState.nToken, compiledOut, nCompiledOut);
  }
  if (rightState.isStopwordNoHit) {
    return execute_query_build_single_token_compiled(leftState.token, leftState.nToken, compiledOut, nCompiledOut);
  }
  return execute_query_build_simple_boolean_compiled(
      leftState.token, leftState.nToken, opAnd, rightState.token, rightState.nToken, compiledOut, nCompiledOut);
}

static int execute_query_wrap_compiled_expression(const char *expr, int nExpr, char **compiledOut, int *nCompiledOut) {
  char *compiled = NULL;
  int nCompiled = 0;
  int compiledCap = 0;
  int rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, '(');
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, expr, nExpr);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, ')');
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(compiled);
    return rc;
  }
  compiled[nCompiled] = '\0';
  *compiledOut = compiled;
  *nCompiledOut = nCompiled;
  return SQLITE_OK;
}

typedef struct ExecutePhraseTerm {
  char *term;
  int nTerm;
  int relPosition;
} ExecutePhraseTerm;

typedef struct ExecuteTermOffsetEntry {
  char *term;
  int nTerm;
  int *offsets;
  int count;
  int cap;
} ExecuteTermOffsetEntry;

static void execute_query_free_phrase_terms(ExecutePhraseTerm *terms, int termCount) {
  if (terms == NULL) {
    return;
  }
  for (int i = 0; i < termCount; i++) {
    sqlite3_free(terms[i].term);
  }
  sqlite3_free(terms);
}

static int execute_query_collect_phrase_terms(const char *text, int nText, ExecutePhraseTerm **termsOut, int *termCountOut) {
  *termsOut = NULL;
  *termCountOut = 0;
  if (text == NULL || nText <= 0) {
    return SQLITE_OK;
  }

  ArabicTokenizer tokenizer;
  memset(&tokenizer, 0, sizeof(tokenizer));
  AnalyzeTermsBuffer analyzed;
  memset(&analyzed, 0, sizeof(analyzed));
  int rc = arabic_tokenize(
      (Fts5Tokenizer *)&tokenizer,
      &analyzed,
      FTS5_TOKENIZE_QUERY | ARABIC_TOKENIZE_EMIT_POSITIONS,
      text,
      nText,
      analyze_collect_token_with_position);
  if (rc == SQLITE_OK) {
    rc = analyzed.rc;
  }
  if (rc != SQLITE_OK) {
    analyze_terms_buffer_free(&analyzed);
    return rc;
  }
  if (analyzed.count <= 0) {
    analyze_terms_buffer_free(&analyzed);
    return SQLITE_OK;
  }

  int analyzedCount = analyzed.count;
  ExecutePhraseTerm *terms = ar_malloc_array(analyzedCount, sizeof(ExecutePhraseTerm));
  if (terms == NULL) {
    analyze_terms_buffer_free(&analyzed);
    return SQLITE_NOMEM;
  }
  memset(terms, 0, (size_t)analyzedCount * sizeof(ExecutePhraseTerm));
  int firstPosition = analyzed.positions[0];
  for (int i = 0; i < analyzedCount; i++) {
    terms[i].nTerm = analyzed.termLens[i];
    int termCap = 0;
    if (ar_checked_int_sum(terms[i].nTerm, 1, &termCap) != SQLITE_OK) {
      execute_query_free_phrase_terms(terms, analyzed.count);
      analyze_terms_buffer_free(&analyzed);
      return SQLITE_TOOBIG;
    }
    terms[i].term = sqlite3_malloc64((sqlite3_uint64)termCap);
    if (terms[i].term == NULL) {
      execute_query_free_phrase_terms(terms, analyzed.count);
      analyze_terms_buffer_free(&analyzed);
      return SQLITE_NOMEM;
    }
    memcpy(terms[i].term, analyzed.terms[i], (size_t)terms[i].nTerm);
    terms[i].term[terms[i].nTerm] = '\0';
    int rel = analyzed.positions[i] - firstPosition;
    terms[i].relPosition = (rel < 0) ? 0 : rel;
  }
  analyze_terms_buffer_free(&analyzed);
  *termsOut = terms;
  *termCountOut = analyzedCount;
  return SQLITE_OK;
}

static int execute_query_build_phrase_compiled(
    const ExecutePhraseTerm *terms,
    int termCount,
    int explicitSlop,
    char **compiledOut,
    int *nCompiledOut) {
  if (termCount <= 0 || terms == NULL) {
    return execute_query_build_single_token_compiled("__nohit__", 9, compiledOut, nCompiledOut);
  }
  if (termCount == 1) {
    return execute_query_build_single_token_compiled(terms[0].term, terms[0].nTerm, compiledOut, nCompiledOut);
  }

  int implicitSlop = terms[termCount - 1].relPosition - (termCount - 1);
  if (implicitSlop < 0) {
    implicitSlop = 0;
  }
  int effectiveSlop = explicitSlop + implicitSlop;
  if (effectiveSlop < 0) {
    effectiveSlop = 0;
  }

  char *compiled = NULL;
  int nCompiled = 0;
  int compiledCap = 0;
  int rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, '(');
  if (effectiveSlop <= 0) {
    if (rc == SQLITE_OK) {
      rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, '"');
    }
    for (int i = 0; rc == SQLITE_OK && i < termCount; i++) {
      if (i > 0) {
        rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, ' ');
        if (rc != SQLITE_OK) {
          break;
        }
      }
      for (int j = 0; rc == SQLITE_OK && j < terms[i].nTerm; j++) {
        if (terms[i].term[j] == '"') {
          rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, "\"\"", 2);
        } else {
          rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, terms[i].term[j]);
        }
      }
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, '"');
    }
  } else {
    if (rc == SQLITE_OK) {
      rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, "NEAR(", 5);
    }
    for (int i = 0; rc == SQLITE_OK && i < termCount; i++) {
      if (i > 0) {
        rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, ' ');
        if (rc != SQLITE_OK) {
          break;
        }
      }
      rc = execute_query_append_match_term(&compiled, &nCompiled, &compiledCap, terms[i].term, terms[i].nTerm);
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, ", ", 2);
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_int(&compiled, &nCompiled, &compiledCap, effectiveSlop);
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, ')');
    }
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, ')');
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(compiled);
    return rc;
  }
  compiled[nCompiled] = '\0';
  *compiledOut = compiled;
  *nCompiledOut = nCompiled;
  return SQLITE_OK;
}

static void execute_query_free_term_offsets(ExecuteTermOffsetEntry *entries, int entryCount) {
  if (entries == NULL) {
    return;
  }
  for (int i = 0; i < entryCount; i++) {
    sqlite3_free(entries[i].term);
    sqlite3_free(entries[i].offsets);
  }
  sqlite3_free(entries);
}

static int execute_query_find_term_offsets(const ExecuteTermOffsetEntry *entries, int entryCount, const char *term, int nTerm) {
  for (int i = 0; i < entryCount; i++) {
    if (entries[i].nTerm == nTerm && memcmp(entries[i].term, term, (size_t)nTerm) == 0) {
      return i;
    }
  }
  return -1;
}

static int execute_query_term_offsets_append(
    ExecuteTermOffsetEntry **entries,
    int *entryCount,
    int *entryCap,
    const char *term,
    int nTerm,
    int offset) {
  int idx = execute_query_find_term_offsets(*entries, *entryCount, term, nTerm);
  if (idx < 0) {
    if (*entryCount >= *entryCap) {
      int required = 0;
      int rc = ar_checked_int_sum(*entryCount, 1, &required);
      if (rc != SQLITE_OK) {
        return rc;
      }
      int nextCap = 0;
      rc = ar_grow_capacity(*entryCap, required, 8, &nextCap);
      if (rc != SQLITE_OK) {
        return rc;
      }
      ExecuteTermOffsetEntry *next = ar_realloc_array(*entries, nextCap, sizeof(ExecuteTermOffsetEntry));
      if (next == NULL) {
        return SQLITE_NOMEM;
      }
      *entries = next;
      *entryCap = nextCap;
    }
    idx = *entryCount;
    int termCap = 0;
    int rc = ar_checked_int_sum(nTerm, 1, &termCap);
    if (rc != SQLITE_OK) {
      return rc;
    }
    char *termCopy = sqlite3_malloc64((sqlite3_uint64)termCap);
    if (termCopy == NULL) {
      return SQLITE_NOMEM;
    }
    memcpy(termCopy, term, (size_t)nTerm);
    termCopy[nTerm] = '\0';
    (*entries)[idx].term = termCopy;
    (*entries)[idx].nTerm = nTerm;
    (*entries)[idx].offsets = NULL;
    (*entries)[idx].count = 0;
    (*entries)[idx].cap = 0;
    *entryCount = idx + 1;
  }

  if ((*entries)[idx].count >= (*entries)[idx].cap) {
    int required = 0;
    int rc = ar_checked_int_sum((*entries)[idx].count, 1, &required);
    if (rc != SQLITE_OK) {
      return rc;
    }
    int nextCap = 0;
    rc = ar_grow_capacity((*entries)[idx].cap, required, 8, &nextCap);
    if (rc != SQLITE_OK) {
      return rc;
    }
    int *nextOffsets = ar_realloc_array((*entries)[idx].offsets, nextCap, sizeof(int));
    if (nextOffsets == NULL) {
      return SQLITE_NOMEM;
    }
    (*entries)[idx].offsets = nextOffsets;
    (*entries)[idx].cap = nextCap;
  }
  (*entries)[idx].offsets[(*entries)[idx].count++] = offset;
  return SQLITE_OK;
}

static int execute_query_build_term_offsets_from_text(
    const char *text,
    int nText,
    ExecuteTermOffsetEntry **entriesOut,
    int *entryCountOut) {
  *entriesOut = NULL;
  *entryCountOut = 0;
  if (text == NULL || nText <= 0) {
    return SQLITE_OK;
  }

  ArabicTokenizer tokenizer;
  memset(&tokenizer, 0, sizeof(tokenizer));
  AnalyzeTermsBuffer analyzed;
  memset(&analyzed, 0, sizeof(analyzed));
  int rc = arabic_tokenize(
      (Fts5Tokenizer *)&tokenizer,
      &analyzed,
      FTS5_TOKENIZE_QUERY | ARABIC_TOKENIZE_EMIT_POSITIONS,
      text,
      nText,
      analyze_collect_token_with_position);
  if (rc == SQLITE_OK) {
    rc = analyzed.rc;
  }
  if (rc != SQLITE_OK) {
    analyze_terms_buffer_free(&analyzed);
    return rc;
  }

  ExecuteTermOffsetEntry *entries = NULL;
  int entryCount = 0;
  int entryCap = 0;
  for (int i = 0; i < analyzed.count; i++) {
    rc = execute_query_term_offsets_append(
        &entries, &entryCount, &entryCap, analyzed.terms[i], analyzed.termLens[i], analyzed.positions[i]);
    if (rc != SQLITE_OK) {
      break;
    }
  }
  analyze_terms_buffer_free(&analyzed);
  if (rc != SQLITE_OK) {
    execute_query_free_term_offsets(entries, entryCount);
    return rc;
  }
  *entriesOut = entries;
  *entryCountOut = entryCount;
  return SQLITE_OK;
}

static int execute_query_upper_bound_int(const int *values, int nValues, int needle) {
  int lo = 0;
  int hi = nValues;
  while (lo < hi) {
    int mid = lo + ((hi - lo) / 2);
    if (values[mid] <= needle) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

static int execute_query_phrase_freq_dfs(
    const int **offsetsByTerm,
    const int *countsByTerm,
    const int *phrasePositions,
    int termCount,
    int slop,
    int termIndex,
    int firstOffset,
    int prevOffset) {
  if (termIndex >= termCount) {
    if (firstOffset < 0 || prevOffset < 0) {
      return 0;
    }
    int spanLimit = phrasePositions[termCount - 1];
    int extra = (prevOffset - firstOffset) - spanLimit;
    return (extra <= slop) ? 1 : 0;
  }

  const int *offsets = offsetsByTerm[termIndex];
  int nOffsets = countsByTerm[termIndex];
  int startPos = (prevOffset < 0) ? 0 : execute_query_upper_bound_int(offsets, nOffsets, prevOffset);
  int count = 0;
  for (int i = startPos; i < nOffsets; i++) {
    int offset = offsets[i];
    if (firstOffset < 0) {
      count += execute_query_phrase_freq_dfs(
          offsetsByTerm, countsByTerm, phrasePositions, termCount, slop, termIndex + 1, offset, offset);
      continue;
    }
    int minimumExtra = (offset - firstOffset) - phrasePositions[termIndex];
    if (minimumExtra > slop) {
      break;
    }
    count += execute_query_phrase_freq_dfs(
        offsetsByTerm, countsByTerm, phrasePositions, termCount, slop, termIndex + 1, firstOffset, offset);
  }
  return count;
}

static int execute_query_count_phrase_frequency(
    const ExecuteTermOffsetEntry *entries,
    int entryCount,
    const ExecutePhraseTerm *phraseTerms,
    int termCount,
    int slop) {
  if (termCount < 2 || phraseTerms == NULL) {
    return 0;
  }
  const int **offsetsByTerm = ar_malloc_array(termCount, sizeof(int *));
  int *countsByTerm = ar_malloc_array(termCount, sizeof(int));
  int *phrasePositions = ar_malloc_array(termCount, sizeof(int));
  if (offsetsByTerm == NULL || countsByTerm == NULL || phrasePositions == NULL) {
    sqlite3_free(offsetsByTerm);
    sqlite3_free(countsByTerm);
    sqlite3_free(phrasePositions);
    return 0;
  }

  for (int i = 0; i < termCount; i++) {
    int idx = execute_query_find_term_offsets(entries, entryCount, phraseTerms[i].term, phraseTerms[i].nTerm);
    if (idx < 0 || entries[idx].count <= 0) {
      sqlite3_free(offsetsByTerm);
      sqlite3_free(countsByTerm);
      sqlite3_free(phrasePositions);
      return 0;
    }
    offsetsByTerm[i] = entries[idx].offsets;
    countsByTerm[i] = entries[idx].count;
    phrasePositions[i] = phraseTerms[i].relPosition;
  }

  int freq = execute_query_phrase_freq_dfs(
      offsetsByTerm, countsByTerm, phrasePositions, termCount, slop, 0, -1, -1);
  sqlite3_free(offsetsByTerm);
  sqlite3_free(countsByTerm);
  sqlite3_free(phrasePositions);
  return freq;
}

static int execute_query_collect_phrase_hits(
    sqlite3 *db,
    int fieldKind,
    const ExecutePhraseTerm *phraseTerms,
    int phraseTermCount,
    int phraseSlop,
    int limit,
    ExecuteQueryHit **hitsOut,
    int *hitCountOut) {
  *hitsOut = NULL;
  *hitCountOut = 0;
  if (phraseTerms == NULL || phraseTermCount <= 0 || limit <= 0) {
    return SQLITE_OK;
  }
  if (phraseTermCount == 1) {
    return execute_query_collect_field_hits(
        db, fieldKind, phraseTerms[0].term, phraseTerms[0].nTerm, limit, hitsOut, hitCountOut);
  }

  ExecuteQueryBookLen *lens = NULL;
  int lensCount = 0;
  int docCount = 0;
  float avgdl = 0.0f;
  int rc = execute_query_load_field_stats(db, fieldKind, &lens, &lensCount, &docCount, &avgdl);
  if (rc != SQLITE_OK) {
    return rc;
  }
  if (docCount <= 0 || avgdl <= 0.0f) {
    sqlite3_free(lens);
    return SQLITE_OK;
  }

  float weight = 0.0f;
  for (int i = 0; i < phraseTermCount; i++) {
    int docFreq = 0;
    rc = execute_query_load_doc_freq(db, fieldKind, phraseTerms[i].term, phraseTerms[i].nTerm, &docFreq);
    if (rc != SQLITE_OK) {
      sqlite3_free(lens);
      return rc;
    }
    if (docFreq <= 0) {
      sqlite3_free(lens);
      return SQLITE_OK;
    }
    float idf = 0.0f;
    rc = execute_query_lucene_idf_via_sql(db, docCount, docFreq, &idf);
    if (rc != SQLITE_OK) {
      sqlite3_free(lens);
      return rc;
    }
    weight = execute_query_f32_add(weight, idf);
  }

  const char *candidateSql = (fieldKind == 1)
                                 ? "SELECT m.rowid, m.book_id, m.page_id FROM qcv_page_vocab_inst v JOIN page_doc_map m ON m.rowid = v.doc WHERE v.term = ? GROUP BY m.rowid, m.book_id, m.page_id"
                                 : "SELECT m.rowid, m.book_id, m.title_id FROM qcv_title_vocab_inst v JOIN title_doc_map m ON m.rowid = v.doc WHERE v.term = ? GROUP BY m.rowid, m.book_id, m.title_id";
  const char *sourceSql = (fieldKind == 1) ? "SELECT body FROM page_content_store WHERE rowid = ?"
                                           : "SELECT title FROM title_content_store WHERE rowid = ?";

  sqlite3_stmt *candidateStmt = NULL;
  sqlite3_stmt *sourceStmt = NULL;
  sqlite3_stmt *scoreStmt = NULL;
  rc = sqlite3_prepare_v2(db, candidateSql, -1, &candidateStmt, NULL);
  if (rc == SQLITE_OK) {
    rc = sqlite3_prepare_v2(db, sourceSql, -1, &sourceStmt, NULL);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_prepare_v2(db, "SELECT sqlite_tokenizer_ar_lucene_term_score(?, ?, ?, ?, ?, ?)", -1, &scoreStmt, NULL);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(candidateStmt, 1, phraseTerms[0].term, phraseTerms[0].nTerm, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    sqlite3_finalize(candidateStmt);
    sqlite3_finalize(sourceStmt);
    sqlite3_finalize(scoreStmt);
    sqlite3_free(lens);
    return rc;
  }

  ExecuteQueryHit *hits = NULL;
  int hitCount = 0;
  int hitCap = 0;
  while ((rc = sqlite3_step(candidateStmt)) == SQLITE_ROW) {
    int rowid = sqlite3_column_int(candidateStmt, 0);
    int bookId = sqlite3_column_int(candidateStmt, 1);
    int itemId = sqlite3_column_int(candidateStmt, 2);

    sqlite3_reset(sourceStmt);
    sqlite3_clear_bindings(sourceStmt);
    int stepRc = sqlite3_bind_int(sourceStmt, 1, rowid);
    if (stepRc != SQLITE_OK) {
      rc = stepRc;
      break;
    }
    stepRc = sqlite3_step(sourceStmt);
    if (stepRc != SQLITE_ROW) {
      if (stepRc == SQLITE_DONE) {
        continue;
      }
      rc = stepRc;
      break;
    }
    const char *rawText = (const char *)sqlite3_column_text(sourceStmt, 0);
    int nRawText = sqlite3_column_bytes(sourceStmt, 0);
    if (rawText == NULL || nRawText <= 0) {
      continue;
    }

    ExecuteTermOffsetEntry *entries = NULL;
    int entryCount = 0;
    int entryRc = execute_query_build_term_offsets_from_text(rawText, nRawText, &entries, &entryCount);
    if (entryRc != SQLITE_OK) {
      execute_query_free_term_offsets(entries, entryCount);
      rc = entryRc;
      break;
    }
    int phraseFreq = execute_query_count_phrase_frequency(entries, entryCount, phraseTerms, phraseTermCount, phraseSlop);
    execute_query_free_term_offsets(entries, entryCount);
    if (phraseFreq <= 0) {
      continue;
    }

    int rawDocLen = execute_query_lookup_doc_len(lens, lensCount, bookId);
    sqlite3_reset(scoreStmt);
    sqlite3_clear_bindings(scoreStmt);
    stepRc = sqlite3_bind_double(scoreStmt, 1, (double)weight);
    if (stepRc == SQLITE_OK) {
      stepRc = sqlite3_bind_double(scoreStmt, 2, (double)phraseFreq);
    }
    if (stepRc == SQLITE_OK) {
      stepRc = sqlite3_bind_int(scoreStmt, 3, rawDocLen);
    }
    if (stepRc == SQLITE_OK) {
      stepRc = sqlite3_bind_double(scoreStmt, 4, 1.2);
    }
    if (stepRc == SQLITE_OK) {
      stepRc = sqlite3_bind_double(scoreStmt, 5, 0.75);
    }
    if (stepRc == SQLITE_OK) {
      stepRc = sqlite3_bind_double(scoreStmt, 6, (double)avgdl);
    }
    if (stepRc != SQLITE_OK) {
      rc = stepRc;
      break;
    }
    stepRc = sqlite3_step(scoreStmt);
    if (stepRc != SQLITE_ROW) {
      rc = stepRc;
      break;
    }
    float score = -(float)sqlite3_column_double(scoreStmt, 0);
    rc = execute_query_hits_append(&hits, &hitCount, &hitCap, fieldKind, bookId, itemId, score);
    if (rc != SQLITE_OK) {
      break;
    }
  }
  int candidateFinalizeRc = sqlite3_finalize(candidateStmt);
  int sourceFinalizeRc = sqlite3_finalize(sourceStmt);
  int scoreFinalizeRc = sqlite3_finalize(scoreStmt);
  sqlite3_free(lens);
  if (rc == SQLITE_DONE) {
    rc = SQLITE_OK;
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(hits);
    if (candidateFinalizeRc != SQLITE_OK) {
      return candidateFinalizeRc;
    }
    if (sourceFinalizeRc != SQLITE_OK) {
      return sourceFinalizeRc;
    }
    if (scoreFinalizeRc != SQLITE_OK) {
      return scoreFinalizeRc;
    }
    return rc;
  }
  if (candidateFinalizeRc != SQLITE_OK) {
    sqlite3_free(hits);
    return candidateFinalizeRc;
  }
  if (sourceFinalizeRc != SQLITE_OK) {
    sqlite3_free(hits);
    return sourceFinalizeRc;
  }
  if (scoreFinalizeRc != SQLITE_OK) {
    sqlite3_free(hits);
    return scoreFinalizeRc;
  }

  if (hitCount > 1) {
    qsort(hits, (size_t)hitCount, sizeof(ExecuteQueryHit), execute_query_hit_compare_field);
  }
  if (hitCount > limit) {
    hitCount = limit;
  }
  *hitsOut = hits;
  *hitCountOut = hitCount;
  return SQLITE_OK;
}

static int execute_query_collect_bm25_hits(
    sqlite3 *db,
    int fieldKind,
    const char *matchExpr,
    int nMatchExpr,
    int limit,
    ExecuteQueryHit **hitsOut,
    int *hitCountOut) {
  *hitsOut = NULL;
  *hitCountOut = 0;
  if (matchExpr == NULL || nMatchExpr <= 0 || limit <= 0) {
    return SQLITE_OK;
  }

  const char *sql = (fieldKind == 1)
                        ? "SELECT m.book_id, m.page_id, bm25(page_fts) AS score FROM page_fts JOIN page_doc_map m ON m.rowid = page_fts.rowid WHERE page_fts MATCH ? ORDER BY score, m.book_id, m.page_id LIMIT ?"
                        : "SELECT m.book_id, m.title_id, bm25(title_fts) AS score FROM title_fts JOIN title_doc_map m ON m.rowid = title_fts.rowid WHERE title_fts MATCH ? ORDER BY score, m.book_id, m.title_id LIMIT ?";
  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = sqlite3_bind_text(stmt, 1, matchExpr, nMatchExpr, SQLITE_TRANSIENT);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 2, limit);
  }
  if (rc != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return rc;
  }

  ExecuteQueryHit *hits = NULL;
  int hitCount = 0;
  int hitCap = 0;
  while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
    int bookId = sqlite3_column_int(stmt, 0);
    int itemId = sqlite3_column_int(stmt, 1);
    double score = sqlite3_column_double(stmt, 2);
    rc = execute_query_hits_append(&hits, &hitCount, &hitCap, fieldKind, bookId, itemId, score);
    if (rc != SQLITE_OK) {
      break;
    }
  }
  int finalizeRc = sqlite3_finalize(stmt);
  if (rc == SQLITE_DONE) {
    rc = SQLITE_OK;
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(hits);
    if (finalizeRc != SQLITE_OK) {
      return finalizeRc;
    }
    return rc;
  }
  if (finalizeRc != SQLITE_OK) {
    sqlite3_free(hits);
    return finalizeRc;
  }
  *hitsOut = hits;
  *hitCountOut = hitCount;
  return SQLITE_OK;
}

static int execute_query_hit_has_analyzed_term(
    sqlite3 *db,
    int fieldKind,
    int bookId,
    int itemId,
    const char *term,
    int nTerm,
    int *hasMatchOut) {
  *hasMatchOut = 0;
  if (term == NULL || nTerm <= 0) {
    return SQLITE_OK;
  }
  const char *sql = (fieldKind == 1)
                        ? "SELECT 1 FROM qcv_page_vocab_inst v JOIN page_doc_map m ON m.rowid = v.doc WHERE m.book_id = ? AND m.page_id = ? AND v.term = ? LIMIT 1"
                        : "SELECT 1 FROM qcv_title_vocab_inst v JOIN title_doc_map m ON m.rowid = v.doc WHERE m.book_id = ? AND m.title_id = ? AND v.term = ? LIMIT 1";
  sqlite3_stmt *stmt = NULL;
  int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = sqlite3_bind_int(stmt, 1, bookId);
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_int(stmt, 2, itemId);
  }
  if (rc == SQLITE_OK) {
    rc = sqlite3_bind_text(stmt, 3, term, nTerm, SQLITE_TRANSIENT);
  }
  if (rc != SQLITE_OK) {
    sqlite3_finalize(stmt);
    return rc;
  }
  rc = sqlite3_step(stmt);
  if (rc == SQLITE_ROW) {
    *hasMatchOut = 1;
    rc = SQLITE_OK;
  } else if (rc == SQLITE_DONE) {
    rc = SQLITE_OK;
  }
  int finalizeRc = sqlite3_finalize(stmt);
  if (rc != SQLITE_OK) {
    return rc;
  }
  if (finalizeRc != SQLITE_OK) {
    return finalizeRc;
  }
  return SQLITE_OK;
}

static int execute_query_apply_scoped_boolean_token_boosts(
    sqlite3 *db,
    int fieldKind,
    const ExecuteParsedTokenClause *left,
    const ExecuteParsedTokenClause *right,
    ExecuteQueryHit *hits,
    int hitCount) {
  if (hits == NULL || hitCount <= 0) {
    return SQLITE_OK;
  }

  int leftRelevant = ((left->scopeFieldKind == 0 || left->scopeFieldKind == fieldKind) &&
                      fabs((double)(left->boost - 1.0f)) >= 1e-12 &&
                      left->analyzedCount == 1 &&
                      left->analyzedTerm != NULL &&
                      left->nAnalyzedTerm > 0);
  int rightRelevant = ((right->scopeFieldKind == 0 || right->scopeFieldKind == fieldKind) &&
                       fabs((double)(right->boost - 1.0f)) >= 1e-12 &&
                       right->analyzedCount == 1 &&
                       right->analyzedTerm != NULL &&
                       right->nAnalyzedTerm > 0);
  if (!leftRelevant && !rightRelevant) {
    return SQLITE_OK;
  }

  int rc = SQLITE_OK;
  for (int i = 0; i < hitCount; i++) {
    float factor = 1.0f;
    if (leftRelevant) {
      int hasLeft = 0;
      rc = execute_query_hit_has_analyzed_term(
          db, fieldKind, hits[i].bookId, hits[i].itemId, left->analyzedTerm, left->nAnalyzedTerm, &hasLeft);
      if (rc != SQLITE_OK) {
        return rc;
      }
      if (hasLeft) {
        factor = execute_query_f32_mul(factor, left->boost);
      }
    }
    if (rightRelevant) {
      int hasRight = 0;
      rc = execute_query_hit_has_analyzed_term(
          db, fieldKind, hits[i].bookId, hits[i].itemId, right->analyzedTerm, right->nAnalyzedTerm, &hasRight);
      if (rc != SQLITE_OK) {
        return rc;
      }
      if (hasRight) {
        factor = execute_query_f32_mul(factor, right->boost);
      }
    }
    if (fabs((double)(factor - 1.0f)) >= 1e-12) {
      hits[i].score = (double)execute_query_f32_mul((float)hits[i].score, factor);
    }
  }

  if (hitCount > 1) {
    qsort(hits, (size_t)hitCount, sizeof(ExecuteQueryHit), execute_query_hit_compare_field);
  }
  return SQLITE_OK;
}

typedef struct ExecuteBooleanClause {
  int isPhrase;
  int scopeFieldKind; /* 0=none, 1=page, 2=title */
  float boost;
  ExecuteParsedTokenClause token;
  ExecutePhraseTerm *phraseTerms;
  int phraseTermCount;
  int phraseSlop;
} ExecuteBooleanClause;

static void execute_query_free_boolean_clause(ExecuteBooleanClause *clause) {
  if (clause == NULL) {
    return;
  }
  execute_query_free_parsed_token_clause(&clause->token);
  execute_query_free_phrase_terms(clause->phraseTerms, clause->phraseTermCount);
  memset(clause, 0, sizeof(*clause));
}

static int execute_query_parse_boolean_clause(const ParsedSimpleAstClause *clause, ExecuteBooleanClause *out) {
  memset(out, 0, sizeof(*out));
  out->boost = 1.0f;
  if (token_equals_ascii_ci(clause->kind, clause->nKind, "token")) {
    int rc = execute_query_parse_token_clause(clause, &out->token);
    if (rc != 0) {
      execute_query_free_boolean_clause(out);
    } else {
      out->scopeFieldKind = out->token.scopeFieldKind;
    }
    return rc;
  }
  if (token_equals_ascii_ci(clause->kind, clause->nKind, "phrase") ||
      token_equals_ascii_ci(clause->kind, clause->nKind, "phrase_slop") ||
      token_equals_ascii_ci(clause->kind, clause->nKind, "field_phrase") ||
      token_equals_ascii_ci(clause->kind, clause->nKind, "field_phrase_slop")) {
    int hasScope = token_equals_ascii_ci(clause->kind, clause->nKind, "field_phrase") ||
                   token_equals_ascii_ci(clause->kind, clause->nKind, "field_phrase_slop");
    int hasSlop = token_equals_ascii_ci(clause->kind, clause->nKind, "phrase_slop") ||
                  token_equals_ascii_ci(clause->kind, clause->nKind, "field_phrase_slop");

    const char *phrasePayload = clause->value;
    int nPhrasePayload = clause->nValue;
    int scopeFieldKind = 0;
    if (hasScope) {
      const char *scope = NULL;
      int nScope = 0;
      const char *scopedRaw = NULL;
      int nScopedRaw = 0;
      parse_boosted_token_clause_scope(clause->value, clause->nValue, &scope, &nScope, &scopedRaw, &nScopedRaw);
      if (scope == NULL || scopedRaw == NULL || nScopedRaw <= 0) {
        return 1;
      }
      scopeFieldKind = execute_query_field_kind(scope, nScope);
      if (scopeFieldKind == 0) {
        return 1;
      }
      phrasePayload = scopedRaw;
      nPhrasePayload = nScopedRaw;
    }

    const char *phraseText = phrasePayload;
    int nPhraseText = nPhrasePayload;
    int phraseSlop = 0;
    if (hasSlop) {
      int splitAt = find_unescaped_char_span(phrasePayload, 0, nPhrasePayload, '\t');
      if (splitAt < 0 || splitAt + 1 >= nPhrasePayload) {
        return 1;
      }
      int nSlop = splitAt;
      if (!parse_nonnegative_int_span(phrasePayload, nSlop, INT_MAX, &phraseSlop)) {
        return 1;
      }
      phraseText = phrasePayload + splitAt + 1;
      nPhraseText = nPhrasePayload - (splitAt + 1);
    }
    int rc = execute_query_collect_phrase_terms(phraseText, nPhraseText, &out->phraseTerms, &out->phraseTermCount);
    if (rc != SQLITE_OK) {
      execute_query_free_boolean_clause(out);
      return 3;
    }
    out->isPhrase = 1;
    out->scopeFieldKind = scopeFieldKind;
    out->phraseSlop = phraseSlop;
    return 0;
  }
  return 1;
}

static int execute_query_parse_phrase_boolean_boosts(
    const char *query,
    int nQuery,
    int *leftKindOut,
    int *rightKindOut,
    int *opAndOut,
    float *leftBoostOut,
    float *rightBoostOut) {
  int start = 0;
  int end = 0;
  trim_ascii_space_span(query, nQuery, &start, &end);
  if (end <= start) {
    return 0;
  }

  ParsedPhraseTermBooleanClause left;
  ParsedPhraseTermBooleanClause right;
  int idx = start;
  if (!parse_phrase_term_boolean_clause(query, idx, end, &left, &idx)) {
    return 0;
  }

  int spacing = 0;
  while (idx < end && isspace((unsigned char)query[idx])) {
    idx++;
    spacing++;
  }
  if (spacing <= 0) {
    return 0;
  }

  int opStart = idx;
  while (idx < end && !isspace((unsigned char)query[idx])) {
    idx++;
  }
  int nOp = idx - opStart;
  if (nOp <= 0) {
    return 0;
  }
  const char *op = query + opStart;
  int opAnd = token_equals_ascii_ci(op, nOp, "AND");
  if (!opAnd && !token_equals_ascii_ci(op, nOp, "OR")) {
    return 0;
  }

  spacing = 0;
  while (idx < end && isspace((unsigned char)query[idx])) {
    idx++;
    spacing++;
  }
  if (spacing <= 0) {
    return 0;
  }

  int rightParsed = 0;
  if (left.kind == 2) {
    if (parse_phrase_term_boolean_phrase_clause(query, idx, end, &right, &idx)) {
      rightParsed = 1;
    } else if (parse_phrase_term_boolean_term_clause(query, idx, end, &right, &idx)) {
      rightParsed = 1;
    }
  } else {
    if (parse_phrase_term_boolean_phrase_clause(query, idx, end, &right, &idx)) {
      rightParsed = 1;
    }
  }
  if (!rightParsed) {
    return 0;
  }

  while (idx < end && isspace((unsigned char)query[idx])) {
    idx++;
  }
  if (idx != end || (left.kind == 1 && right.kind == 1)) {
    return 0;
  }

  double leftBoost = 0.0;
  double rightBoost = 0.0;
  if (!parse_boost_literal_double_cstr(left.boostBuf, &leftBoost) || !parse_boost_literal_double_cstr(right.boostBuf, &rightBoost)) {
    return 0;
  }

  *leftKindOut = left.kind;
  *rightKindOut = right.kind;
  *opAndOut = opAnd;
  *leftBoostOut = (float)leftBoost;
  *rightBoostOut = (float)rightBoost;
  return 1;
}

static int execute_query_collect_boolean_clause_hits(
    sqlite3 *db,
    int fieldKind,
    const ExecuteBooleanClause *clause,
    int limit,
    ExecuteQueryHit **hitsOut,
    int *hitCountOut) {
  *hitsOut = NULL;
  *hitCountOut = 0;
  if (clause->scopeFieldKind != 0 && clause->scopeFieldKind != fieldKind) {
    return SQLITE_OK;
  }
  if (clause->isPhrase) {
    int rc = execute_query_collect_phrase_hits(
        db, fieldKind, clause->phraseTerms, clause->phraseTermCount, clause->phraseSlop, limit, hitsOut, hitCountOut);
    if (rc != SQLITE_OK) {
      return rc;
    }
    execute_query_apply_boost_inplace(*hitsOut, *hitCountOut, clause->boost);
    return SQLITE_OK;
  }
  if (clause->token.analyzedCount != 1 || clause->token.analyzedTerm == NULL || clause->token.nAnalyzedTerm <= 0) {
    return SQLITE_OK;
  }
  int rc = execute_query_collect_field_hits(
      db, fieldKind, clause->token.analyzedTerm, clause->token.nAnalyzedTerm, limit, hitsOut, hitCountOut);
  if (rc != SQLITE_OK) {
    return rc;
  }
  execute_query_apply_boost_inplace(*hitsOut, *hitCountOut, clause->token.boost);
  return SQLITE_OK;
}

static int execute_query_merge_boolean_hits(
    int fieldKind,
    int opAnd,
    const ExecuteQueryHit *leftHits,
    int leftCount,
    const ExecuteQueryHit *rightHits,
    int rightCount,
    int limit,
    ExecuteQueryHit **hitsOut,
    int *hitCountOut) {
  *hitsOut = NULL;
  *hitCountOut = 0;
  ExecuteQueryHit *merged = NULL;
  int mergedCount = 0;
  int mergedCap = 0;
  int rc = SQLITE_OK;
  if (opAnd) {
    for (int i = 0; i < leftCount; i++) {
      int rightIdx = execute_query_find_hit_index(rightHits, rightCount, leftHits[i].bookId, leftHits[i].itemId);
      if (rightIdx < 0) {
        continue;
      }
      float score = execute_query_f32_add(leftHits[i].score, rightHits[rightIdx].score);
      rc = execute_query_hits_append(
          &merged, &mergedCount, &mergedCap, fieldKind, leftHits[i].bookId, leftHits[i].itemId, score);
      if (rc != SQLITE_OK) {
        break;
      }
    }
  } else {
    for (int i = 0; i < leftCount; i++) {
      rc = execute_query_hits_append(
          &merged, &mergedCount, &mergedCap, fieldKind, leftHits[i].bookId, leftHits[i].itemId, leftHits[i].score);
      if (rc != SQLITE_OK) {
        break;
      }
    }
    for (int i = 0; rc == SQLITE_OK && i < rightCount; i++) {
      int idx = execute_query_find_hit_index(merged, mergedCount, rightHits[i].bookId, rightHits[i].itemId);
      if (idx >= 0) {
        merged[idx].score = execute_query_f32_add(merged[idx].score, rightHits[i].score);
        continue;
      }
      rc = execute_query_hits_append(
          &merged, &mergedCount, &mergedCap, fieldKind, rightHits[i].bookId, rightHits[i].itemId, rightHits[i].score);
    }
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(merged);
    return rc;
  }
  if (mergedCount > 1) {
    qsort(merged, (size_t)mergedCount, sizeof(ExecuteQueryHit), execute_query_hit_compare_field);
  }
  if (mergedCount > limit) {
    mergedCount = limit;
  }
  *hitsOut = merged;
  *hitCountOut = mergedCount;
  return SQLITE_OK;
}

static int execute_query_append_boolean_clause_compiled(
    const ExecuteBooleanClause *clause,
    int fieldKind,
    char **compiled,
    int *nCompiled,
    int *compiledCap) {
  if (fieldKind != 0 && clause->scopeFieldKind != 0 && clause->scopeFieldKind != fieldKind) {
    return execute_query_append_match_term(compiled, nCompiled, compiledCap, "__nohit__", 9);
  }
  if (clause->isPhrase) {
    char *phraseCompiled = NULL;
    int nPhraseCompiled = 0;
    int rc = execute_query_build_phrase_compiled(
        clause->phraseTerms, clause->phraseTermCount, clause->phraseSlop, &phraseCompiled, &nPhraseCompiled);
    if (rc != SQLITE_OK) {
      sqlite3_free(phraseCompiled);
      return rc;
    }
    if (nPhraseCompiled >= 2 && phraseCompiled[0] == '(' && phraseCompiled[nPhraseCompiled - 1] == ')') {
      rc = json_builder_append_bytes(compiled, nCompiled, compiledCap, phraseCompiled + 1, nPhraseCompiled - 2);
    } else {
      rc = json_builder_append_bytes(compiled, nCompiled, compiledCap, phraseCompiled, nPhraseCompiled);
    }
    sqlite3_free(phraseCompiled);
    return rc;
  }
  ExecuteCompiledTokenState tokenState;
  memset(&tokenState, 0, sizeof(tokenState));
  execute_query_resolve_compiled_token_state_for_field(&clause->token, fieldKind, &tokenState);
  return execute_query_append_match_term(compiled, nCompiled, compiledCap, tokenState.token, tokenState.nToken);
}

static int execute_query_build_mixed_boolean_compiled(
    const ExecuteBooleanClause *left,
    int opAnd,
    const ExecuteBooleanClause *right,
    int fieldKind,
    char **compiledOut,
    int *nCompiledOut) {
  char *compiled = NULL;
  int nCompiled = 0;
  int compiledCap = 0;
  int rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, '(');
  if (rc == SQLITE_OK) {
    rc = execute_query_append_boolean_clause_compiled(left, fieldKind, &compiled, &nCompiled, &compiledCap);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, opAnd ? ") AND (" : ") OR (", opAnd ? 7 : 6);
  }
  if (rc == SQLITE_OK) {
    rc = execute_query_append_boolean_clause_compiled(right, fieldKind, &compiled, &nCompiled, &compiledCap);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, ')');
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(compiled);
    return rc;
  }
  compiled[nCompiled] = '\0';
  *compiledOut = compiled;
  *nCompiledOut = nCompiled;
  return SQLITE_OK;
}

static int execute_query_build_mixed_not_boolean_compiled(
    const ExecuteBooleanClause *positive,
    const ExecuteBooleanClause *negative,
    int fieldKind,
    char **compiledOut,
    int *nCompiledOut) {
  char *compiled = NULL;
  int nCompiled = 0;
  int compiledCap = 0;
  int rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, "((", 2);
  if (rc == SQLITE_OK) {
    rc = execute_query_append_boolean_clause_compiled(positive, fieldKind, &compiled, &nCompiled, &compiledCap);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, ")) NOT ((", 9);
  }
  if (rc == SQLITE_OK) {
    rc = execute_query_append_boolean_clause_compiled(negative, fieldKind, &compiled, &nCompiled, &compiledCap);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, "))", 2);
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(compiled);
    return rc;
  }
  compiled[nCompiled] = '\0';
  *compiledOut = compiled;
  *nCompiledOut = nCompiled;
  return SQLITE_OK;
}

static int execute_query_clause_is_group_like(const ParsedSimpleAstClause *clause) {
  return token_equals_ascii_ci(clause->kind, clause->nKind, "group") || token_equals_ascii_ci(clause->kind, clause->nKind, "field_group") ||
         token_equals_ascii_ci(clause->kind, clause->nKind, "field_group_boost");
}

static int execute_query_parse_field_group_clause_value(
    const ParsedSimpleAstClause *clause,
    const char **scopeOut,
    int *nScopeOut,
    const char **innerOut,
    int *nInnerOut) {
  int firstTab = -1;
  int secondTab = -1;
  for (int i = 0; i < clause->nValue; i++) {
    if (clause->value[i] != '\t') {
      continue;
    }
    if (firstTab < 0) {
      firstTab = i;
    } else {
      secondTab = i;
      break;
    }
  }
  if (firstTab <= 0 || firstTab >= clause->nValue - 1) {
    return 0;
  }

  const char *scope = clause->value;
  int nScope = firstTab;
  const char *inner = NULL;
  int nInner = 0;
  if (token_equals_ascii_ci(clause->kind, clause->nKind, "field_group")) {
    inner = clause->value + firstTab + 1;
    nInner = clause->nValue - (firstTab + 1);
  } else if (token_equals_ascii_ci(clause->kind, clause->nKind, "field_group_boost")) {
    if (secondTab <= firstTab + 1 || secondTab >= clause->nValue - 1) {
      return 0;
    }
    inner = clause->value + secondTab + 1;
    nInner = clause->nValue - (secondTab + 1);
  } else {
    return 0;
  }
  if (inner == NULL || nInner <= 0) {
    return 0;
  }

  *scopeOut = scope;
  *nScopeOut = nScope;
  *innerOut = inner;
  *nInnerOut = nInner;
  return 1;
}

static int execute_query_append_wrapped_expression(char **json, int *jsonLen, int *jsonCap, const char *expr, int nExpr) {
  int rc = json_builder_append_bytes(json, jsonLen, jsonCap, "(", 1);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = json_builder_append_bytes(json, jsonLen, jsonCap, expr, nExpr);
  if (rc != SQLITE_OK) {
    return rc;
  }
  return json_builder_append_bytes(json, jsonLen, jsonCap, ")", 1);
}

static int execute_query_build_unwrapped_single_term_expression(const char *term, int nTerm, char **compiledOut, int *nCompiledOut) {
  char *compiled = NULL;
  int nCompiled = 0;
  int compiledCap = 0;
  int rc = execute_query_append_match_term(&compiled, &nCompiled, &compiledCap, term, nTerm);
  if (rc != SQLITE_OK) {
    sqlite3_free(compiled);
    return rc;
  }
  compiled[nCompiled] = '\0';
  *compiledOut = compiled;
  *nCompiledOut = nCompiled;
  return SQLITE_OK;
}

static int execute_query_unwrap_outer_parens(char **compiledInOut, int *nCompiledInOut) {
  if (compiledInOut == NULL || *compiledInOut == NULL || nCompiledInOut == NULL || *nCompiledInOut < 2) {
    return SQLITE_OK;
  }
  char *compiled = *compiledInOut;
  int nCompiled = *nCompiledInOut;
  if (compiled[0] != '(' || compiled[nCompiled - 1] != ')') {
    return SQLITE_OK;
  }
  int nInner = nCompiled - 2;
  int innerCap = 0;
  if (ar_checked_int_sum(nInner, 1, &innerCap) != SQLITE_OK) {
    return SQLITE_TOOBIG;
  }
  char *inner = sqlite3_malloc64((sqlite3_uint64)innerCap);
  if (inner == NULL) {
    return SQLITE_NOMEM;
  }
  if (nInner > 0) {
    memcpy(inner, compiled + 1, (size_t)nInner);
  }
  inner[nInner] = '\0';
  sqlite3_free(compiled);
  *compiledInOut = inner;
  *nCompiledInOut = nInner;
  return SQLITE_OK;
}

static int execute_query_compile_parsed_clauses_for_field(
    const ParsedSimpleAstClause *clauses,
    int clauseCount,
    int runtimeFieldKind,
    char **compiledOut,
    int *nCompiledOut);

static int execute_query_compile_phrase_clause_for_field(
    const ParsedSimpleAstClause *clause,
    int runtimeFieldKind,
    char **compiledOut,
    int *nCompiledOut) {
  int hasScope = token_equals_ascii_ci(clause->kind, clause->nKind, "field_phrase") ||
                 token_equals_ascii_ci(clause->kind, clause->nKind, "field_phrase_slop");
  int hasSlop = token_equals_ascii_ci(clause->kind, clause->nKind, "phrase_slop") ||
                token_equals_ascii_ci(clause->kind, clause->nKind, "field_phrase_slop");

  const char *phrasePayload = clause->value;
  int nPhrasePayload = clause->nValue;
  if (hasScope) {
    const char *scope = NULL;
    int nScope = 0;
    const char *scopedRaw = NULL;
    int nScopedRaw = 0;
    parse_boosted_token_clause_scope(clause->value, clause->nValue, &scope, &nScope, &scopedRaw, &nScopedRaw);
    if (scope == NULL || scopedRaw == NULL || nScopedRaw <= 0) {
      return EXECUTE_QUERY_COMPILE_UNSUPPORTED;
    }
    int scopeFieldKind = execute_query_field_kind(scope, nScope);
    if (scopeFieldKind == 0) {
      return EXECUTE_QUERY_COMPILE_UNSUPPORTED;
    }
    if (scopeFieldKind != runtimeFieldKind) {
      int rcNoHit = execute_query_build_unwrapped_single_term_expression("__nohit__", 9, compiledOut, nCompiledOut);
      return (rcNoHit == SQLITE_OK) ? EXECUTE_QUERY_COMPILE_OK : EXECUTE_QUERY_COMPILE_NOMEM;
    }
    phrasePayload = scopedRaw;
    nPhrasePayload = nScopedRaw;
  }

  int phraseSlop = 0;
  const char *phraseText = phrasePayload;
  int nPhraseText = nPhrasePayload;
  if (hasSlop) {
    int splitAt = find_unescaped_char_span(phrasePayload, 0, nPhrasePayload, '\t');
    if (splitAt < 0 || splitAt + 1 >= nPhrasePayload) {
      return EXECUTE_QUERY_COMPILE_UNSUPPORTED;
    }
    int nSlop = splitAt;
    if (!parse_nonnegative_int_span(phrasePayload, nSlop, INT_MAX, &phraseSlop)) {
      return EXECUTE_QUERY_COMPILE_UNSUPPORTED;
    }
    phraseText = phrasePayload + splitAt + 1;
    nPhraseText = nPhrasePayload - (splitAt + 1);
  }

  ExecutePhraseTerm *phraseTerms = NULL;
  int phraseTermCount = 0;
  int rcPhrase = execute_query_collect_phrase_terms(phraseText, nPhraseText, &phraseTerms, &phraseTermCount);
  if (rcPhrase != SQLITE_OK) {
    return EXECUTE_QUERY_COMPILE_NOMEM;
  }
  int rcCompile = execute_query_build_phrase_compiled(phraseTerms, phraseTermCount, phraseSlop, compiledOut, nCompiledOut);
  execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
  if (rcCompile != SQLITE_OK) {
    return EXECUTE_QUERY_COMPILE_NOMEM;
  }
  int rcUnwrap = execute_query_unwrap_outer_parens(compiledOut, nCompiledOut);
  if (rcUnwrap != SQLITE_OK) {
    sqlite3_free(*compiledOut);
    *compiledOut = NULL;
    *nCompiledOut = 0;
    return EXECUTE_QUERY_COMPILE_NOMEM;
  }
  return EXECUTE_QUERY_COMPILE_OK;
}

static int execute_query_compile_clause_for_field(
    const ParsedSimpleAstClause *clause,
    int runtimeFieldKind,
    char **compiledOut,
    int *nCompiledOut) {
  if (token_equals_ascii_ci(clause->kind, clause->nKind, "token")) {
    ExecuteParsedTokenClause tokenClause;
    memset(&tokenClause, 0, sizeof(tokenClause));
    int parseRc = execute_query_parse_token_clause(clause, &tokenClause);
    if (parseRc == 3) {
      execute_query_free_parsed_token_clause(&tokenClause);
      return EXECUTE_QUERY_COMPILE_NOMEM;
    }
    if (parseRc != 0) {
      execute_query_free_parsed_token_clause(&tokenClause);
      return EXECUTE_QUERY_COMPILE_UNSUPPORTED;
    }
    ExecuteCompiledTokenState tokenState;
    memset(&tokenState, 0, sizeof(tokenState));
    execute_query_resolve_compiled_token_state_for_field(&tokenClause, runtimeFieldKind, &tokenState);
    int rcCompile = execute_query_build_unwrapped_single_term_expression(tokenState.token, tokenState.nToken, compiledOut, nCompiledOut);
    execute_query_free_parsed_token_clause(&tokenClause);
    return (rcCompile == SQLITE_OK) ? EXECUTE_QUERY_COMPILE_OK : EXECUTE_QUERY_COMPILE_NOMEM;
  }

  if (token_equals_ascii_ci(clause->kind, clause->nKind, "phrase") || token_equals_ascii_ci(clause->kind, clause->nKind, "phrase_slop") ||
      token_equals_ascii_ci(clause->kind, clause->nKind, "field_phrase") ||
      token_equals_ascii_ci(clause->kind, clause->nKind, "field_phrase_slop")) {
    return execute_query_compile_phrase_clause_for_field(clause, runtimeFieldKind, compiledOut, nCompiledOut);
  }

  if (token_equals_ascii_ci(clause->kind, clause->nKind, "group")) {
    ParsedSimpleAstClause innerA;
    ParsedSimpleAstClause innerB;
    int innerCount = 0;
    if (!parse_simple_query_ast_clauses(clause->value, clause->nValue, &innerA, &innerB, &innerCount)) {
      return EXECUTE_QUERY_COMPILE_UNSUPPORTED;
    }
    ParsedSimpleAstClause innerClauses[2];
    if (innerCount > 0) {
      innerClauses[0] = innerA;
    }
    if (innerCount > 1) {
      innerClauses[1] = innerB;
    }
    return execute_query_compile_parsed_clauses_for_field(innerClauses, innerCount, runtimeFieldKind, compiledOut, nCompiledOut);
  }

  if (token_equals_ascii_ci(clause->kind, clause->nKind, "field_group") ||
      token_equals_ascii_ci(clause->kind, clause->nKind, "field_group_boost")) {
    const char *scope = NULL;
    int nScope = 0;
    const char *innerQuery = NULL;
    int nInnerQuery = 0;
    if (!execute_query_parse_field_group_clause_value(clause, &scope, &nScope, &innerQuery, &nInnerQuery)) {
      return EXECUTE_QUERY_COMPILE_UNSUPPORTED;
    }
    int scopeFieldKind = execute_query_field_kind(scope, nScope);
    if (scopeFieldKind == 0) {
      return EXECUTE_QUERY_COMPILE_UNSUPPORTED;
    }
    if (scopeFieldKind != runtimeFieldKind) {
      int rcNoHit = execute_query_build_unwrapped_single_term_expression("__nohit__", 9, compiledOut, nCompiledOut);
      return (rcNoHit == SQLITE_OK) ? EXECUTE_QUERY_COMPILE_OK : EXECUTE_QUERY_COMPILE_NOMEM;
    }
    ParsedSimpleAstClause innerA;
    ParsedSimpleAstClause innerB;
    int innerCount = 0;
    if (!parse_simple_query_ast_clauses(innerQuery, nInnerQuery, &innerA, &innerB, &innerCount)) {
      return EXECUTE_QUERY_COMPILE_UNSUPPORTED;
    }
    ParsedSimpleAstClause innerClauses[2];
    if (innerCount > 0) {
      innerClauses[0] = innerA;
    }
    if (innerCount > 1) {
      innerClauses[1] = innerB;
    }
    return execute_query_compile_parsed_clauses_for_field(innerClauses, innerCount, runtimeFieldKind, compiledOut, nCompiledOut);
  }

  return EXECUTE_QUERY_COMPILE_UNSUPPORTED;
}

static int execute_query_compile_parsed_clauses_for_field(
    const ParsedSimpleAstClause *clauses,
    int clauseCount,
    int runtimeFieldKind,
    char **compiledOut,
    int *nCompiledOut) {
  if (clauseCount <= 0) {
    int rcNoHit = execute_query_build_unwrapped_single_term_expression("__nohit__", 9, compiledOut, nCompiledOut);
    return (rcNoHit == SQLITE_OK) ? EXECUTE_QUERY_COMPILE_OK : EXECUTE_QUERY_COMPILE_NOMEM;
  }

  char *compiledNodes[2] = {NULL, NULL};
  int nCompiledNodes[2] = {0, 0};
  int occurKinds[2] = {0, 0}; /* 1=must, 2=should, 3=must_not */
  int mustCount = 0;
  int shouldCount = 0;
  int mustNotCount = 0;

  for (int i = 0; i < clauseCount; i++) {
    int nodeRc = execute_query_compile_clause_for_field(&clauses[i], runtimeFieldKind, &compiledNodes[i], &nCompiledNodes[i]);
    if (nodeRc != EXECUTE_QUERY_COMPILE_OK) {
      sqlite3_free(compiledNodes[0]);
      sqlite3_free(compiledNodes[1]);
      return nodeRc;
    }
    int occurKind = 2;
    if (token_equals_ascii_ci(clauses[i].occur, clauses[i].nOccur, "MUST")) {
      occurKind = 1;
      mustCount++;
    } else if (token_equals_ascii_ci(clauses[i].occur, clauses[i].nOccur, "MUST_NOT")) {
      occurKind = 3;
      mustNotCount++;
    } else {
      shouldCount++;
    }
    occurKinds[i] = occurKind;
  }

  char *positive = NULL;
  int nPositive = 0;
  int positiveCap = 0;
  int positiveOccurKind = (mustCount > 0) ? 1 : 2;
  int positiveCount = (mustCount > 0) ? mustCount : shouldCount;
  if (positiveCount > 0) {
    int appended = 0;
    for (int i = 0; i < clauseCount; i++) {
      if (occurKinds[i] != positiveOccurKind) {
        continue;
      }
      int rc = SQLITE_OK;
      if (appended > 0) {
        rc = json_builder_append_bytes(
            &positive, &nPositive, &positiveCap, (positiveOccurKind == 1) ? " AND " : " OR ", (positiveOccurKind == 1) ? 5 : 4);
      }
      if (rc == SQLITE_OK) {
        rc = execute_query_append_wrapped_expression(&positive, &nPositive, &positiveCap, compiledNodes[i], nCompiledNodes[i]);
      }
      if (rc != SQLITE_OK) {
        sqlite3_free(compiledNodes[0]);
        sqlite3_free(compiledNodes[1]);
        sqlite3_free(positive);
        return EXECUTE_QUERY_COMPILE_NOMEM;
      }
      appended++;
    }
  }
  sqlite3_free(compiledNodes[0]);
  sqlite3_free(compiledNodes[1]);

  if (positiveCount <= 0 || nPositive <= 0) {
    sqlite3_free(positive);
    int rcNoHit = execute_query_build_unwrapped_single_term_expression("__nohit__", 9, compiledOut, nCompiledOut);
    return (rcNoHit == SQLITE_OK) ? EXECUTE_QUERY_COMPILE_OK : EXECUTE_QUERY_COMPILE_NOMEM;
  }
  if (mustNotCount <= 0) {
    positive[nPositive] = '\0';
    *compiledOut = positive;
    *nCompiledOut = nPositive;
    return EXECUTE_QUERY_COMPILE_OK;
  }

  char *compiled = NULL;
  int nCompiled = 0;
  int compiledCap = 0;
  int rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, "(", 1);
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, positive, nPositive);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, ") NOT (", 7);
  }
  int appendedMustNot = 0;
  for (int i = 0; rc == SQLITE_OK && i < clauseCount; i++) {
    if (occurKinds[i] != 3) {
      continue;
    }
    char *mustNotExpr = NULL;
    int nMustNotExpr = 0;
    int nodeRc = execute_query_compile_clause_for_field(&clauses[i], runtimeFieldKind, &mustNotExpr, &nMustNotExpr);
    if (nodeRc != EXECUTE_QUERY_COMPILE_OK) {
      sqlite3_free(positive);
      sqlite3_free(compiled);
      sqlite3_free(mustNotExpr);
      return (nodeRc == EXECUTE_QUERY_COMPILE_NOMEM) ? EXECUTE_QUERY_COMPILE_NOMEM : EXECUTE_QUERY_COMPILE_UNSUPPORTED;
    }
    if (appendedMustNot > 0) {
      rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, " OR ", 4);
    }
    if (rc == SQLITE_OK) {
      rc = execute_query_append_wrapped_expression(&compiled, &nCompiled, &compiledCap, mustNotExpr, nMustNotExpr);
    }
    sqlite3_free(mustNotExpr);
    appendedMustNot++;
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&compiled, &nCompiled, &compiledCap, ")", 1);
  }
  sqlite3_free(positive);
  if (rc != SQLITE_OK) {
    sqlite3_free(compiled);
    return EXECUTE_QUERY_COMPILE_NOMEM;
  }
  compiled[nCompiled] = '\0';
  *compiledOut = compiled;
  *nCompiledOut = nCompiled;
  return EXECUTE_QUERY_COMPILE_OK;
}

static int execute_query_compile_query_expression_for_field(
    const char *query,
    int nQuery,
    int runtimeFieldKind,
    char **compiledOut,
    int *nCompiledOut) {
  ParsedSimpleAstClause clauseA;
  ParsedSimpleAstClause clauseB;
  int clauseCount = 0;
  if (!parse_simple_query_ast_clauses(query, nQuery, &clauseA, &clauseB, &clauseCount)) {
    return EXECUTE_QUERY_COMPILE_UNSUPPORTED;
  }
  ParsedSimpleAstClause clauses[2];
  if (clauseCount > 0) {
    clauses[0] = clauseA;
  }
  if (clauseCount > 1) {
    clauses[1] = clauseB;
  }
  return execute_query_compile_parsed_clauses_for_field(clauses, clauseCount, runtimeFieldKind, compiledOut, nCompiledOut);
}

static void execute_query_free_boosted_tokens(ExecuteQueryBoostedToken *tokens, int tokenCount) {
  if (tokens == NULL) {
    return;
  }
  for (int i = 0; i < tokenCount; i++) {
    sqlite3_free(tokens[i].term);
  }
  sqlite3_free(tokens);
}

static int execute_query_boosted_tokens_append(
    ExecuteQueryBoostedToken **tokens,
    int *count,
    int *cap,
    const char *term,
    int nTerm,
    float boost) {
  if (*count >= *cap) {
    int required = 0;
    int rc = ar_checked_int_sum(*count, 1, &required);
    if (rc != SQLITE_OK) {
      return rc;
    }
    int nextCap = 0;
    rc = ar_grow_capacity(*cap, required, 8, &nextCap);
    if (rc != SQLITE_OK) {
      return rc;
    }
    ExecuteQueryBoostedToken *next = ar_realloc_array(*tokens, nextCap, sizeof(ExecuteQueryBoostedToken));
    if (next == NULL) {
      return SQLITE_NOMEM;
    }
    *tokens = next;
    *cap = nextCap;
  }
  int termCopyCap = 0;
  if (ar_checked_int_sum(nTerm, 1, &termCopyCap) != SQLITE_OK) {
    return SQLITE_TOOBIG;
  }
  char *termCopy = sqlite3_malloc64((sqlite3_uint64)termCopyCap);
  if (termCopy == NULL) {
    return SQLITE_NOMEM;
  }
  memcpy(termCopy, term, (size_t)nTerm);
  termCopy[nTerm] = '\0';
  (*tokens)[*count].term = termCopy;
  (*tokens)[*count].nTerm = nTerm;
  (*tokens)[*count].boost = boost;
  (*count)++;
  return SQLITE_OK;
}

static int execute_query_collect_boosted_tokens_from_clauses(
    const ParsedSimpleAstClause *clauses,
    int clauseCount,
    int runtimeFieldKind,
    ExecuteQueryBoostedToken **tokensOut,
    int *tokenCountOut,
    int *tokenCapOut) {
  for (int i = 0; i < clauseCount; i++) {
    const ParsedSimpleAstClause *clause = &clauses[i];
    if (token_equals_ascii_ci(clause->kind, clause->nKind, "token")) {
      ExecuteParsedTokenClause tokenClause;
      memset(&tokenClause, 0, sizeof(tokenClause));
      int parseRc = execute_query_parse_token_clause(clause, &tokenClause);
      if (parseRc == 3) {
        execute_query_free_parsed_token_clause(&tokenClause);
        return SQLITE_NOMEM;
      }
      if (parseRc == 0 && tokenClause.analyzedCount == 1 && tokenClause.analyzedTerm != NULL && tokenClause.nAnalyzedTerm > 0 &&
          fabs((double)(tokenClause.boost - 1.0f)) >= 1e-12 &&
          (tokenClause.scopeFieldKind == 0 || tokenClause.scopeFieldKind == runtimeFieldKind)) {
        int rcAppend = execute_query_boosted_tokens_append(
            tokensOut, tokenCountOut, tokenCapOut, tokenClause.analyzedTerm, tokenClause.nAnalyzedTerm, tokenClause.boost);
        execute_query_free_parsed_token_clause(&tokenClause);
        if (rcAppend != SQLITE_OK) {
          return rcAppend;
        }
        continue;
      }
      execute_query_free_parsed_token_clause(&tokenClause);
      continue;
    }

    if (token_equals_ascii_ci(clause->kind, clause->nKind, "group")) {
      ParsedSimpleAstClause innerA;
      ParsedSimpleAstClause innerB;
      int innerCount = 0;
      if (!parse_simple_query_ast_clauses(clause->value, clause->nValue, &innerA, &innerB, &innerCount)) {
        continue;
      }
      ParsedSimpleAstClause innerClauses[2];
      if (innerCount > 0) {
        innerClauses[0] = innerA;
      }
      if (innerCount > 1) {
        innerClauses[1] = innerB;
      }
      int rcInner = execute_query_collect_boosted_tokens_from_clauses(
          innerClauses, innerCount, runtimeFieldKind, tokensOut, tokenCountOut, tokenCapOut);
      if (rcInner != SQLITE_OK) {
        return rcInner;
      }
      continue;
    }

    if (token_equals_ascii_ci(clause->kind, clause->nKind, "field_group") ||
        token_equals_ascii_ci(clause->kind, clause->nKind, "field_group_boost")) {
      const char *scope = NULL;
      int nScope = 0;
      const char *innerQuery = NULL;
      int nInnerQuery = 0;
      if (!execute_query_parse_field_group_clause_value(clause, &scope, &nScope, &innerQuery, &nInnerQuery)) {
        continue;
      }
      int scopeFieldKind = execute_query_field_kind(scope, nScope);
      if (scopeFieldKind == 0 || scopeFieldKind != runtimeFieldKind) {
        continue;
      }
      ParsedSimpleAstClause innerA;
      ParsedSimpleAstClause innerB;
      int innerCount = 0;
      if (!parse_simple_query_ast_clauses(innerQuery, nInnerQuery, &innerA, &innerB, &innerCount)) {
        continue;
      }
      ParsedSimpleAstClause innerClauses[2];
      if (innerCount > 0) {
        innerClauses[0] = innerA;
      }
      if (innerCount > 1) {
        innerClauses[1] = innerB;
      }
      int rcInner = execute_query_collect_boosted_tokens_from_clauses(
          innerClauses, innerCount, runtimeFieldKind, tokensOut, tokenCountOut, tokenCapOut);
      if (rcInner != SQLITE_OK) {
        return rcInner;
      }
    }
  }
  return SQLITE_OK;
}

static int execute_query_apply_recursive_token_boosts(
    sqlite3 *db,
    int fieldKind,
    const char *query,
    int nQuery,
    ExecuteQueryHit *hits,
    int hitCount) {
  if (hits == NULL || hitCount <= 0) {
    return SQLITE_OK;
  }
  ParsedSimpleAstClause clauseA;
  ParsedSimpleAstClause clauseB;
  int clauseCount = 0;
  if (!parse_simple_query_ast_clauses(query, nQuery, &clauseA, &clauseB, &clauseCount)) {
    return SQLITE_OK;
  }
  ParsedSimpleAstClause clauses[2];
  if (clauseCount > 0) {
    clauses[0] = clauseA;
  }
  if (clauseCount > 1) {
    clauses[1] = clauseB;
  }

  ExecuteQueryBoostedToken *tokens = NULL;
  int tokenCount = 0;
  int tokenCap = 0;
  int rc = execute_query_collect_boosted_tokens_from_clauses(clauses, clauseCount, fieldKind, &tokens, &tokenCount, &tokenCap);
  if (rc != SQLITE_OK) {
    execute_query_free_boosted_tokens(tokens, tokenCount);
    return rc;
  }
  if (tokenCount <= 0) {
    execute_query_free_boosted_tokens(tokens, tokenCount);
    return SQLITE_OK;
  }

  int anyChanged = 0;
  for (int i = 0; i < hitCount; i++) {
    float factor = 1.0f;
    for (int j = 0; j < tokenCount; j++) {
      int hasTerm = 0;
      rc = execute_query_hit_has_analyzed_term(db, fieldKind, hits[i].bookId, hits[i].itemId, tokens[j].term, tokens[j].nTerm, &hasTerm);
      if (rc != SQLITE_OK) {
        execute_query_free_boosted_tokens(tokens, tokenCount);
        return rc;
      }
      if (hasTerm) {
        factor = execute_query_f32_mul(factor, tokens[j].boost);
      }
    }
    if (fabs((double)(factor - 1.0f)) >= 1e-12) {
      hits[i].score = (double)execute_query_f32_mul((float)hits[i].score, factor);
      anyChanged = 1;
    }
  }
  execute_query_free_boosted_tokens(tokens, tokenCount);
  if (anyChanged && hitCount > 1) {
    qsort(hits, (size_t)hitCount, sizeof(ExecuteQueryHit), execute_query_hit_compare_field);
  }
  return SQLITE_OK;
}

static void udf_sqlite_tokenizer_ar_execute_query_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 4) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_execute_query_json expects 4 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL ||
      sqlite3_value_type(argv[2]) == SQLITE_NULL) {
    sqlite3_result_error(context, "query, field, and limit are required", -1);
    return;
  }

  const char *query = (const char *)sqlite3_value_text(argv[0]);
  int nQuery = sqlite3_value_bytes(argv[0]);
  const char *field = (const char *)sqlite3_value_text(argv[1]);
  int nField = sqlite3_value_bytes(argv[1]);
  int limit = sqlite3_value_int(argv[2]);
  const char *optionsJson = NULL;
  int nOptionsJson = 0;
  if (sqlite3_value_type(argv[3]) != SQLITE_NULL) {
    optionsJson = (const char *)sqlite3_value_text(argv[3]);
    nOptionsJson = sqlite3_value_bytes(argv[3]);
  }
  int lenientParseErrors = execute_query_contains_true_option(optionsJson, nOptionsJson, "lenient_parse_errors");

  if (query == NULL || nQuery <= 0) {
    sqlite3_result_error(context, "query is empty after parsing", -1);
    return;
  }
  int queryStart = 0;
  int queryEnd = 0;
  trim_ascii_space_span(query, nQuery, &queryStart, &queryEnd);
  if (queryStart >= queryEnd) {
    sqlite3_result_error(context, "query is empty after parsing", -1);
    return;
  }
  if (limit < 1) {
    sqlite3_result_error(context, "limit must be >= 1", -1);
    return;
  }

  float topLevelBoost = 1.0f;
  int forcedScopeFieldKind = 0;
  int forceBm25Execution = 0;
  int wrapForcedScopeCompiled = 0;
  const char *groupInner = NULL;
  int nGroupInner = 0;
  char groupBoostBuf[64];
  int nGroupBoost = 0;
  if (parse_top_level_group_boost_span(
          query + queryStart,
          queryEnd - queryStart,
          &groupInner,
          &nGroupInner,
          groupBoostBuf,
          (int)sizeof(groupBoostBuf),
          &nGroupBoost)) {
    if (groupInner != NULL && nGroupInner > 0 && nGroupBoost > 0) {
      char boostBuf[64];
      if (nGroupBoost >= (int)sizeof(boostBuf)) {
        sqlite3_result_error(context, "invalid group boost", -1);
        return;
      }
      memcpy(boostBuf, groupBoostBuf, (size_t)nGroupBoost);
      boostBuf[nGroupBoost] = '\0';
      double parsedBoost = 0.0;
      if (!parse_boost_literal_double_cstr(boostBuf, &parsedBoost)) {
        sqlite3_result_error(context, "invalid group boost", -1);
        return;
      }
      topLevelBoost = (float)parsedBoost;
      query = groupInner;
      nQuery = nGroupInner;
      queryStart = 0;
      queryEnd = nQuery;
    }
  }

  char scopedGroupScratch[8192];
  const char *scopedGroupScope = NULL;
  int nScopedGroupScope = 0;
  const char *scopedGroupInner = NULL;
  int nScopedGroupInner = 0;
  const char *scopedGroupBoost = NULL;
  int nScopedGroupBoost = 0;
  if (parse_whole_scoped_group_parts(
          query + queryStart,
          queryEnd - queryStart,
          scopedGroupScratch,
          (int)sizeof(scopedGroupScratch),
          &scopedGroupScope,
          &nScopedGroupScope,
          &scopedGroupInner,
          &nScopedGroupInner,
          &scopedGroupBoost,
          &nScopedGroupBoost)) {
    int parsedScopeKind = execute_query_field_kind(scopedGroupScope, nScopedGroupScope);
    if (parsedScopeKind == 0) {
      sqlite3_result_error(context, "unsupported field scope", -1);
      return;
    }
    forcedScopeFieldKind = parsedScopeKind;
    if (scopedGroupBoost != NULL && nScopedGroupBoost > 0) {
      char boostBuf[64];
      if (nScopedGroupBoost >= (int)sizeof(boostBuf)) {
        sqlite3_result_error(context, "invalid group boost", -1);
        return;
      }
      memcpy(boostBuf, scopedGroupBoost, (size_t)nScopedGroupBoost);
      boostBuf[nScopedGroupBoost] = '\0';
      double parsedBoost = 0.0;
      if (!parse_boost_literal_double_cstr(boostBuf, &parsedBoost)) {
        sqlite3_result_error(context, "invalid group boost", -1);
        return;
      }
      topLevelBoost = execute_query_f32_mul(topLevelBoost, (float)parsedBoost);
    }
    query = scopedGroupInner;
    nQuery = nScopedGroupInner;
    queryStart = 0;
    queryEnd = nQuery;
  }
  if (forcedScopeFieldKind == 0) {
    const char *mustOccur = NULL;
    int nMustOccur = 0;
    const char *mustKind = NULL;
    int nMustKind = 0;
    const char *mustValue = NULL;
    int nMustValue = 0;
    if (parse_simple_field_group_clause(
            query + queryStart,
            queryEnd - queryStart,
            &mustOccur,
            &nMustOccur,
            &mustKind,
            &nMustKind,
            &mustValue,
            &nMustValue,
            scopedGroupScratch,
            (int)sizeof(scopedGroupScratch)) &&
        nMustOccur == 4 &&
        memcmp(mustOccur, "MUST", 4) == 0 &&
        ((nMustKind == 11 && memcmp(mustKind, "field_group", 11) == 0) ||
         (nMustKind == 17 && memcmp(mustKind, "field_group_boost", 17) == 0))) {
      int firstTab = -1;
      int secondTab = -1;
      for (int i = 0; i < nMustValue; i++) {
        if (mustValue[i] != '\t') {
          continue;
        }
        if (firstTab < 0) {
          firstTab = i;
        } else {
          secondTab = i;
          break;
        }
      }
      if (firstTab > 0 && firstTab < nMustValue - 1) {
        const char *mustScope = mustValue;
        int nMustScope = firstTab;
        const char *mustInner = NULL;
        int nMustInner = 0;
        const char *mustBoost = NULL;
        int nMustBoost = 0;
        if (nMustKind == 11) {
          mustInner = mustValue + firstTab + 1;
          nMustInner = nMustValue - (firstTab + 1);
        } else if (secondTab > firstTab + 1 && secondTab < nMustValue - 1) {
          mustBoost = mustValue + firstTab + 1;
          nMustBoost = secondTab - (firstTab + 1);
          mustInner = mustValue + secondTab + 1;
          nMustInner = nMustValue - (secondTab + 1);
        }
        if (mustInner != NULL && nMustInner > 0) {
          int mustScopeKind = execute_query_field_kind(mustScope, nMustScope);
          if (mustScopeKind == 0) {
            sqlite3_result_error(context, "unsupported field scope", -1);
            return;
          }
          forcedScopeFieldKind = mustScopeKind;
          forceBm25Execution = 1;
          wrapForcedScopeCompiled = 1;
          if (mustBoost != NULL && nMustBoost > 0) {
            char boostBuf[64];
            if (nMustBoost >= (int)sizeof(boostBuf)) {
              sqlite3_result_error(context, "invalid group boost", -1);
              return;
            }
            memcpy(boostBuf, mustBoost, (size_t)nMustBoost);
            boostBuf[nMustBoost] = '\0';
            double parsedBoost = 0.0;
            if (!parse_boost_literal_double_cstr(boostBuf, &parsedBoost)) {
              sqlite3_result_error(context, "invalid group boost", -1);
              return;
            }
            topLevelBoost = execute_query_f32_mul(topLevelBoost, (float)parsedBoost);
          }
          query = mustInner;
          nQuery = nMustInner;
          queryStart = 0;
          queryEnd = nQuery;
        }
      }
    }
  }

  int requestedFieldKind = execute_query_field_kind(field, nField);
  if (requestedFieldKind == 0) {
    char err[96];
    sqlite3_snprintf((int)sizeof(err), err, "unsupported field selection: %.*s", nField, field);
    sqlite3_result_error(context, err, -1);
    return;
  }

  ParsedSimpleAstClause clauseA;
  ParsedSimpleAstClause clauseB;
  int clauseCount = 0;
  if (!parse_simple_query_ast_clauses(query, nQuery, &clauseA, &clauseB, &clauseCount)) {
    const char *parseError = execute_query_classify_simple_parse_error(query, nQuery);
    if (parseError != NULL) {
      if (!lenientParseErrors) {
        sqlite3_result_error(context, parseError, -1);
        return;
      }
      char *nohit = NULL;
      int nNohit = 0;
      int rc = execute_query_build_nohit_payload(requestedFieldKind, &nohit, &nNohit);
      if (rc != SQLITE_OK) {
        sqlite3_free(nohit);
        sqlite3_result_error_nomem(context);
        return;
      }
      sqlite3_result_text(context, nohit, nNohit, sqlite3_free);
      return;
    }
    sqlite3_result_error(
        context,
        "C backend is not available: execute_query_json currently supports only simple single-token queries",
        -1);
    return;
  }

  int modeBoolean = 0;
  int modeBooleanNot = 0;
  int modeBooleanMixed = 0;
  int modeRecursiveBm25 = 0;
  int modeTokenPattern = 0;
  int modePhrase = 0;
  int opAnd = 0;
  ExecuteParsedTokenClause tokenA;
  ExecuteParsedTokenClause tokenB;
  ExecutePatternTokenClause tokenPattern;
  ExecuteBooleanClause booleanClauseA;
  ExecuteBooleanClause booleanClauseB;
  ExecutePhraseTerm *phraseTerms = NULL;
  int phraseTermCount = 0;
  int phraseSlop = 0;
  float phraseBoost = 1.0f;
  int booleanHasScopedTokens = 0;
  int phraseScopeFieldKind = 0;
  int scoresAreF32 = 1;
  memset(&tokenA, 0, sizeof(tokenA));
  memset(&tokenB, 0, sizeof(tokenB));
  memset(&tokenPattern, 0, sizeof(tokenPattern));
  memset(&booleanClauseA, 0, sizeof(booleanClauseA));
  memset(&booleanClauseB, 0, sizeof(booleanClauseB));

  if ((clauseCount == 1 && execute_query_clause_is_group_like(&clauseA)) ||
      (clauseCount == 2 && (execute_query_clause_is_group_like(&clauseA) || execute_query_clause_is_group_like(&clauseB)))) {
    modeRecursiveBm25 = 1;
  }

  if (!modeRecursiveBm25 && clauseCount == 1) {
    if (token_equals_ascii_ci(clauseA.occur, clauseA.nOccur, "MUST_NOT")) {
      char *nohit = NULL;
      int nNohit = 0;
      int rcNoHit = execute_query_build_nohit_payload(requestedFieldKind, &nohit, &nNohit);
      if (rcNoHit != SQLITE_OK) {
        sqlite3_free(nohit);
        sqlite3_result_error_nomem(context);
        return;
      }
      sqlite3_result_text(context, nohit, nNohit, sqlite3_free);
      return;
    }

    if (token_equals_ascii_ci(clauseA.kind, clauseA.nKind, "token")) {
      int parseRc = execute_query_parse_token_clause(&clauseA, &tokenA);
      if (parseRc == 2) {
        sqlite3_result_error(context, "invalid boosted token", -1);
        return;
      }
      if (parseRc == 3) {
        sqlite3_result_error_nomem(context);
        return;
      }
      if (parseRc != 0) {
        int patternRc = execute_query_parse_pattern_token_clause(&clauseA, &tokenPattern);
        if (patternRc == 2) {
          sqlite3_result_error(context, "invalid boosted token", -1);
          return;
        }
        if (patternRc == 0) {
          modeTokenPattern = 1;
        } else {
          sqlite3_result_error(
              context,
              "C backend is not available: execute_query_json currently supports only plain token queries",
              -1);
          return;
        }
      }
    } else if (token_equals_ascii_ci(clauseA.kind, clauseA.nKind, "phrase") ||
               token_equals_ascii_ci(clauseA.kind, clauseA.nKind, "phrase_slop") ||
               token_equals_ascii_ci(clauseA.kind, clauseA.nKind, "field_phrase") ||
               token_equals_ascii_ci(clauseA.kind, clauseA.nKind, "field_phrase_slop")) {
      int hasScope = token_equals_ascii_ci(clauseA.kind, clauseA.nKind, "field_phrase") ||
                     token_equals_ascii_ci(clauseA.kind, clauseA.nKind, "field_phrase_slop");
      int hasSlop = token_equals_ascii_ci(clauseA.kind, clauseA.nKind, "phrase_slop") ||
                    token_equals_ascii_ci(clauseA.kind, clauseA.nKind, "field_phrase_slop");

      const char *phrasePayload = clauseA.value;
      int nPhrasePayload = clauseA.nValue;
      if (hasScope) {
        const char *scope = NULL;
        int nScope = 0;
        const char *scopedRaw = NULL;
        int nScopedRaw = 0;
        parse_boosted_token_clause_scope(clauseA.value, clauseA.nValue, &scope, &nScope, &scopedRaw, &nScopedRaw);
        if (scope == NULL || scopedRaw == NULL || nScopedRaw <= 0) {
          sqlite3_result_error(
              context, "C backend is not available: execute_query_json currently supports only basic phrase queries", -1);
          return;
        }
        phraseScopeFieldKind = execute_query_field_kind(scope, nScope);
        if (phraseScopeFieldKind == 0) {
          sqlite3_result_error(
              context, "C backend is not available: execute_query_json currently supports only page/title scoped phrases", -1);
          return;
        }
        phrasePayload = scopedRaw;
        nPhrasePayload = nScopedRaw;
      }

      const char *phraseText = phrasePayload;
      int nPhraseText = nPhrasePayload;
      if (hasSlop) {
        int splitAt = find_unescaped_char_span(phrasePayload, 0, nPhrasePayload, '\t');
        if (splitAt < 0 || splitAt + 1 >= nPhrasePayload) {
          sqlite3_result_error(
              context, "C backend is not available: execute_query_json currently supports only basic phrase queries", -1);
          return;
        }
        int nSlop = splitAt;
        if (!parse_nonnegative_int_span(phrasePayload, nSlop, INT_MAX, &phraseSlop)) {
          sqlite3_result_error(
              context, "C backend is not available: execute_query_json currently supports only basic phrase queries", -1);
          return;
        }
        phraseText = phrasePayload + splitAt + 1;
        nPhraseText = nPhrasePayload - (splitAt + 1);
      }

      int rcPhrase = execute_query_collect_phrase_terms(phraseText, nPhraseText, &phraseTerms, &phraseTermCount);
      if (rcPhrase != SQLITE_OK) {
        sqlite3_result_error_nomem(context);
        return;
      }
      ParsedPhraseTermBooleanClause parsedSinglePhrase;
      int parsedNext = 0;
      if (parse_phrase_term_boolean_phrase_clause(query, queryStart, queryEnd, &parsedSinglePhrase, &parsedNext)) {
        while (parsedNext < queryEnd && isspace((unsigned char)query[parsedNext])) {
          parsedNext++;
        }
        if (parsedNext == queryEnd) {
          double parsedBoost = 0.0;
          if (parse_boost_literal_double_cstr(parsedSinglePhrase.boostBuf, &parsedBoost)) {
            phraseBoost = (float)parsedBoost;
          }
          if (parsedSinglePhrase.scope != NULL) {
            int parsedScopeKind = execute_query_field_kind(parsedSinglePhrase.scope, parsedSinglePhrase.nScope);
            if (parsedScopeKind != 0) {
              phraseScopeFieldKind = parsedScopeKind;
            }
          }
        }
      }
      modePhrase = 1;
    } else {
      sqlite3_result_error(
          context,
          "C backend is not available: execute_query_json currently supports only simple token/boolean/phrase queries",
          -1);
      return;
    }
  } else if (!modeRecursiveBm25 && clauseCount == 2) {
    int aMust = token_equals_ascii_ci(clauseA.occur, clauseA.nOccur, "MUST");
    int bMust = token_equals_ascii_ci(clauseB.occur, clauseB.nOccur, "MUST");
    int aShould = token_equals_ascii_ci(clauseA.occur, clauseA.nOccur, "SHOULD");
    int bShould = token_equals_ascii_ci(clauseB.occur, clauseB.nOccur, "SHOULD");
    int aMustNot = token_equals_ascii_ci(clauseA.occur, clauseA.nOccur, "MUST_NOT");
    int bMustNot = token_equals_ascii_ci(clauseB.occur, clauseB.nOccur, "MUST_NOT");
    if (aMust && bMust) {
      modeBoolean = 1;
      opAnd = 1;
    } else if (aShould && bShould) {
      modeBoolean = 1;
      opAnd = 0;
    } else if ((aMust || aShould) && bMustNot) {
      modeBoolean = 1;
      modeBooleanNot = 1;
      opAnd = 0;
    } else if ((bMust || bShould) && aMustNot) {
      ParsedSimpleAstClause tmpClause = clauseA;
      clauseA = clauseB;
      clauseB = tmpClause;
      modeBoolean = 1;
      modeBooleanNot = 1;
      opAnd = 0;
    } else {
      sqlite3_result_error(
          context,
          "C backend is not available: execute_query_json currently supports only simple AND/OR/NOT boolean queries",
          -1);
      return;
    }
    int bothToken = token_equals_ascii_ci(clauseA.kind, clauseA.nKind, "token") && token_equals_ascii_ci(clauseB.kind, clauseB.nKind, "token");
    if (bothToken) {
      int parseLeftRc = execute_query_parse_token_clause(&clauseA, &tokenA);
      if (parseLeftRc == 2) {
        sqlite3_result_error(context, "invalid boosted token", -1);
        return;
      }
      if (parseLeftRc == 3) {
        sqlite3_result_error_nomem(context);
        return;
      }
      if (parseLeftRc != 0) {
        execute_query_free_parsed_token_clause(&tokenA);
        sqlite3_result_error(
            context,
            "C backend is not available: execute_query_json currently supports only plain token AND/OR queries",
            -1);
        return;
      }
      int parseRightRc = execute_query_parse_token_clause(&clauseB, &tokenB);
      if (parseRightRc == 2) {
        execute_query_free_parsed_token_clause(&tokenA);
        sqlite3_result_error(context, "invalid boosted token", -1);
        return;
      }
      if (parseRightRc == 3) {
        execute_query_free_parsed_token_clause(&tokenA);
        sqlite3_result_error_nomem(context);
        return;
      }
      if (parseRightRc != 0) {
        execute_query_free_parsed_token_clause(&tokenA);
        execute_query_free_parsed_token_clause(&tokenB);
        execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
        sqlite3_result_error(
            context,
            "C backend is not available: execute_query_json currently supports only plain token AND/OR queries",
            -1);
        return;
      }
      booleanHasScopedTokens = (tokenA.scopeFieldKind != 0 || tokenB.scopeFieldKind != 0);
    } else {
      int parseLeftRc = execute_query_parse_boolean_clause(&clauseA, &booleanClauseA);
      if (parseLeftRc == 2) {
        sqlite3_result_error(context, "invalid boosted token", -1);
        return;
      }
      if (parseLeftRc == 3) {
        sqlite3_result_error_nomem(context);
        return;
      }
      if (parseLeftRc != 0) {
        execute_query_free_boolean_clause(&booleanClauseA);
        sqlite3_result_error(
            context,
            "C backend is not available: execute_query_json currently supports only simple token/phrase AND/OR queries",
            -1);
        return;
      }
      int parseRightRc = execute_query_parse_boolean_clause(&clauseB, &booleanClauseB);
      if (parseRightRc == 2) {
        execute_query_free_boolean_clause(&booleanClauseA);
        sqlite3_result_error(context, "invalid boosted token", -1);
        return;
      }
      if (parseRightRc == 3) {
        execute_query_free_boolean_clause(&booleanClauseA);
        sqlite3_result_error_nomem(context);
        return;
      }
      if (parseRightRc != 0) {
        execute_query_free_boolean_clause(&booleanClauseA);
        execute_query_free_boolean_clause(&booleanClauseB);
        sqlite3_result_error(
            context,
            "C backend is not available: execute_query_json currently supports only simple token/phrase AND/OR queries",
            -1);
        return;
      }
      if ((!booleanClauseA.isPhrase && booleanClauseA.token.analyzedCount != 1) ||
          (!booleanClauseB.isPhrase && booleanClauseB.token.analyzedCount != 1)) {
        execute_query_free_boolean_clause(&booleanClauseA);
        execute_query_free_boolean_clause(&booleanClauseB);
        sqlite3_result_error(
            context,
            "C backend is not available: execute_query_json currently supports only analyzable token/phrase AND/OR queries",
            -1);
        return;
      }

      int parsedLeftKind = 0;
      int parsedRightKind = 0;
      int parsedOpAnd = 0;
      float parsedLeftBoost = 1.0f;
      float parsedRightBoost = 1.0f;
      if (execute_query_parse_phrase_boolean_boosts(
              query,
              nQuery,
              &parsedLeftKind,
              &parsedRightKind,
              &parsedOpAnd,
              &parsedLeftBoost,
              &parsedRightBoost) &&
          parsedOpAnd == opAnd &&
          (((parsedLeftKind == 2) ? 1 : 0) == booleanClauseA.isPhrase) &&
          (((parsedRightKind == 2) ? 1 : 0) == booleanClauseB.isPhrase)) {
        if (booleanClauseA.isPhrase) {
          booleanClauseA.boost = parsedLeftBoost;
        } else {
          booleanClauseA.token.boost = parsedLeftBoost;
        }
        if (booleanClauseB.isPhrase) {
          booleanClauseB.boost = parsedRightBoost;
        } else {
          booleanClauseB.token.boost = parsedRightBoost;
        }
      }
      modeBooleanMixed = 1;
    }
  } else if (!modeRecursiveBm25) {
    sqlite3_result_error(
        context,
        "C backend is not available: execute_query_json currently supports only simple token/boolean/phrase queries",
        -1);
    return;
  }

  sqlite3 *db = sqlite3_context_db_handle(context);
  int rc = execute_query_ensure_vocab_tables(db);
  if (rc != SQLITE_OK) {
    execute_query_free_parsed_token_clause(&tokenA);
    execute_query_free_parsed_token_clause(&tokenB);
    execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
    execute_query_free_boolean_clause(&booleanClauseA);
    execute_query_free_boolean_clause(&booleanClauseB);
    sqlite3_result_error(context, sqlite3_errmsg(db), -1);
    return;
  }

  char *compiledExpr = NULL;
  int nCompiledExpr = 0;
  char *pageBooleanCompiled = NULL;
  int nPageBooleanCompiled = 0;
  char *titleBooleanCompiled = NULL;
  int nTitleBooleanCompiled = 0;
  const char *pageCompiled = NULL;
  int nPageCompiled = 0;
  const char *titleCompiled = NULL;
  int nTitleCompiled = 0;

  if (modeRecursiveBm25) {
    rc = SQLITE_OK;
    if (requestedFieldKind == 1 || requestedFieldKind == 3) {
      rc = execute_query_compile_query_expression_for_field(query, nQuery, 1, &pageBooleanCompiled, &nPageBooleanCompiled);
    }
    if (rc == SQLITE_OK && (requestedFieldKind == 2 || requestedFieldKind == 3)) {
      rc = execute_query_compile_query_expression_for_field(query, nQuery, 2, &titleBooleanCompiled, &nTitleBooleanCompiled);
    }
    if (rc == EXECUTE_QUERY_COMPILE_UNSUPPORTED) {
      execute_query_free_parsed_token_clause(&tokenA);
      execute_query_free_parsed_token_clause(&tokenB);
      execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
      execute_query_free_boolean_clause(&booleanClauseA);
      execute_query_free_boolean_clause(&booleanClauseB);
      sqlite3_free(compiledExpr);
      sqlite3_free(pageBooleanCompiled);
      sqlite3_free(titleBooleanCompiled);
      sqlite3_result_error(
          context,
          "C backend is not available: execute_query_json currently supports only grouped token/phrase boolean clauses",
          -1);
      return;
    }
    if (rc != SQLITE_OK) {
      execute_query_free_parsed_token_clause(&tokenA);
      execute_query_free_parsed_token_clause(&tokenB);
      execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
      execute_query_free_boolean_clause(&booleanClauseA);
      execute_query_free_boolean_clause(&booleanClauseB);
      sqlite3_free(compiledExpr);
      sqlite3_free(pageBooleanCompiled);
      sqlite3_free(titleBooleanCompiled);
      sqlite3_result_error_nomem(context);
      return;
    }
    pageCompiled = (pageBooleanCompiled != NULL) ? pageBooleanCompiled : "(\"__nohit__\")";
    nPageCompiled = (pageBooleanCompiled != NULL) ? (int)strlen(pageBooleanCompiled) : 13;
    titleCompiled = (titleBooleanCompiled != NULL) ? titleBooleanCompiled : "(\"__nohit__\")";
    nTitleCompiled = (titleBooleanCompiled != NULL) ? (int)strlen(titleBooleanCompiled) : 13;
  } else {
    if (modeTokenPattern) {
      const int wildcardMaxExpansions = 256;
      const int fuzzyMaxExpansions = 128;
      if (requestedFieldKind == 1 || requestedFieldKind == 3) {
        if (tokenPattern.scopeFieldKind != 0 && tokenPattern.scopeFieldKind != 1) {
          rc = execute_query_build_single_token_compiled("__nohit__", 9, &pageBooleanCompiled, &nPageBooleanCompiled);
        } else {
          char **expandedTerms = NULL;
          int expandedCount = 0;
          if (tokenPattern.hasWildcard) {
            rc = execute_query_expand_wildcard_terms(
                db,
                1,
                tokenPattern.baseToken,
                tokenPattern.nBaseToken,
                wildcardMaxExpansions,
                &expandedTerms,
                &expandedCount);
          } else if (tokenPattern.isFuzzy) {
            rc = execute_query_expand_fuzzy_terms(
                db,
                1,
                tokenPattern.fuzzyBase,
                tokenPattern.nFuzzyBase,
                tokenPattern.fuzzyEdits,
                fuzzyMaxExpansions,
                &expandedTerms,
                &expandedCount);
          } else {
            rc = SQLITE_ERROR;
          }
          if (rc == SQLITE_OK) {
            rc = execute_query_build_pattern_compiled_from_terms(expandedTerms, expandedCount, &pageBooleanCompiled, &nPageBooleanCompiled);
          }
          execute_query_free_expanded_terms(expandedTerms, expandedCount);
        }
      }
      if (rc == SQLITE_OK && (requestedFieldKind == 2 || requestedFieldKind == 3)) {
        if (tokenPattern.scopeFieldKind != 0 && tokenPattern.scopeFieldKind != 2) {
          rc = execute_query_build_single_token_compiled("__nohit__", 9, &titleBooleanCompiled, &nTitleBooleanCompiled);
        } else {
          char **expandedTerms = NULL;
          int expandedCount = 0;
          if (tokenPattern.hasWildcard) {
            rc = execute_query_expand_wildcard_terms(
                db,
                2,
                tokenPattern.baseToken,
                tokenPattern.nBaseToken,
                wildcardMaxExpansions,
                &expandedTerms,
                &expandedCount);
          } else if (tokenPattern.isFuzzy) {
            rc = execute_query_expand_fuzzy_terms(
                db,
                2,
                tokenPattern.fuzzyBase,
                tokenPattern.nFuzzyBase,
                tokenPattern.fuzzyEdits,
                fuzzyMaxExpansions,
                &expandedTerms,
                &expandedCount);
          } else {
            rc = SQLITE_ERROR;
          }
          if (rc == SQLITE_OK) {
            rc = execute_query_build_pattern_compiled_from_terms(expandedTerms, expandedCount, &titleBooleanCompiled, &nTitleBooleanCompiled);
          }
          execute_query_free_expanded_terms(expandedTerms, expandedCount);
        }
      }
      if (rc != SQLITE_OK) {
        execute_query_free_parsed_token_clause(&tokenA);
        execute_query_free_parsed_token_clause(&tokenB);
        execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
        execute_query_free_boolean_clause(&booleanClauseA);
        execute_query_free_boolean_clause(&booleanClauseB);
        sqlite3_free(compiledExpr);
        sqlite3_free(pageBooleanCompiled);
        sqlite3_free(titleBooleanCompiled);
        sqlite3_result_error(context, sqlite3_errmsg(db), -1);
        return;
      }
      pageCompiled = (pageBooleanCompiled != NULL) ? pageBooleanCompiled : "(\"__nohit__\")";
      nPageCompiled = (pageBooleanCompiled != NULL) ? (int)strlen(pageBooleanCompiled) : 13;
      titleCompiled = (titleBooleanCompiled != NULL) ? titleBooleanCompiled : "(\"__nohit__\")";
      nTitleCompiled = (titleBooleanCompiled != NULL) ? (int)strlen(titleBooleanCompiled) : 13;

      int patternScopeFieldKind = tokenPattern.scopeFieldKind;
      int runPatternPage = ((requestedFieldKind == 1 || requestedFieldKind == 3) && (patternScopeFieldKind == 0 || patternScopeFieldKind == 1));
      int runPatternTitle = ((requestedFieldKind == 2 || requestedFieldKind == 3) && (patternScopeFieldKind == 0 || patternScopeFieldKind == 2));
      if (!runPatternPage && (requestedFieldKind == 1 || requestedFieldKind == 3)) {
        pageCompiled = "(\"__nohit__\")";
        nPageCompiled = 13;
      }
      if (!runPatternTitle && (requestedFieldKind == 2 || requestedFieldKind == 3)) {
        titleCompiled = "(\"__nohit__\")";
        nTitleCompiled = 13;
      }

      ExecuteQueryHit *patternPageHits = NULL;
      int patternPageHitCount = 0;
      ExecuteQueryHit *patternTitleHits = NULL;
      int patternTitleHitCount = 0;
      if (runPatternPage) {
        if (tokenPattern.hasWildcard) {
          rc = execute_query_collect_constant_hits(db, 1, pageCompiled, nPageCompiled, limit, &patternPageHits, &patternPageHitCount);
        } else {
          rc = execute_query_collect_bm25_hits(db, 1, pageCompiled, nPageCompiled, limit, &patternPageHits, &patternPageHitCount);
        }
        if (rc == SQLITE_OK) {
          execute_query_apply_boost_inplace(patternPageHits, patternPageHitCount, tokenPattern.boost);
        }
        if (rc == SQLITE_OK && fabs((double)(topLevelBoost - 1.0f)) >= 1e-12) {
          execute_query_apply_boost_inplace(patternPageHits, patternPageHitCount, topLevelBoost);
        }
      }
      if (rc == SQLITE_OK && runPatternTitle) {
        if (tokenPattern.hasWildcard) {
          rc = execute_query_collect_constant_hits(db, 2, titleCompiled, nTitleCompiled, limit, &patternTitleHits, &patternTitleHitCount);
        } else {
          rc = execute_query_collect_bm25_hits(db, 2, titleCompiled, nTitleCompiled, limit, &patternTitleHits, &patternTitleHitCount);
        }
        if (rc == SQLITE_OK) {
          execute_query_apply_boost_inplace(patternTitleHits, patternTitleHitCount, tokenPattern.boost);
        }
        if (rc == SQLITE_OK && fabs((double)(topLevelBoost - 1.0f)) >= 1e-12) {
          execute_query_apply_boost_inplace(patternTitleHits, patternTitleHitCount, topLevelBoost);
        }
      }
      if (rc != SQLITE_OK) {
        sqlite3_free(patternPageHits);
        sqlite3_free(patternTitleHits);
        execute_query_free_parsed_token_clause(&tokenA);
        execute_query_free_parsed_token_clause(&tokenB);
        execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
        execute_query_free_boolean_clause(&booleanClauseA);
        execute_query_free_boolean_clause(&booleanClauseB);
        sqlite3_free(compiledExpr);
        sqlite3_free(pageBooleanCompiled);
        sqlite3_free(titleBooleanCompiled);
        sqlite3_result_error(context, sqlite3_errmsg(db), -1);
        return;
      }

      ExecuteQueryHit *merged = NULL;
      int mergedCount = 0;
      int mergedCap = 0;
      for (int i = 0; i < patternPageHitCount; i++) {
        rc = execute_query_hits_append(
            &merged, &mergedCount, &mergedCap, patternPageHits[i].fieldKind, patternPageHits[i].bookId, patternPageHits[i].itemId, patternPageHits[i].score);
        if (rc != SQLITE_OK) {
          break;
        }
      }
      for (int i = 0; rc == SQLITE_OK && i < patternTitleHitCount; i++) {
        rc = execute_query_hits_append(
            &merged, &mergedCount, &mergedCap, patternTitleHits[i].fieldKind, patternTitleHits[i].bookId, patternTitleHits[i].itemId, patternTitleHits[i].score);
      }
      sqlite3_free(patternPageHits);
      sqlite3_free(patternTitleHits);
      if (rc != SQLITE_OK) {
        sqlite3_free(merged);
        execute_query_free_parsed_token_clause(&tokenA);
        execute_query_free_parsed_token_clause(&tokenB);
        execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
        execute_query_free_boolean_clause(&booleanClauseA);
        execute_query_free_boolean_clause(&booleanClauseB);
        sqlite3_free(compiledExpr);
        sqlite3_free(pageBooleanCompiled);
        sqlite3_free(titleBooleanCompiled);
        sqlite3_result_error_nomem(context);
        return;
      }
      if (mergedCount > 1 && requestedFieldKind == 3) {
        qsort(merged, (size_t)mergedCount, sizeof(ExecuteQueryHit), execute_query_hit_compare_merged);
      } else if (mergedCount > 1) {
        qsort(merged, (size_t)mergedCount, sizeof(ExecuteQueryHit), execute_query_hit_compare_field);
      }
      if (mergedCount > limit) {
        mergedCount = limit;
      }

      char *outJson = NULL;
      int nOutJson = 0;
      rc = execute_query_build_payload(
          requestedFieldKind,
          pageCompiled,
          nPageCompiled,
          titleCompiled,
          nTitleCompiled,
          merged,
          mergedCount,
          0,
          &outJson,
          &nOutJson);
      sqlite3_free(merged);
      execute_query_free_parsed_token_clause(&tokenA);
      execute_query_free_parsed_token_clause(&tokenB);
      execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
      execute_query_free_boolean_clause(&booleanClauseA);
      execute_query_free_boolean_clause(&booleanClauseB);
      sqlite3_free(compiledExpr);
      sqlite3_free(pageBooleanCompiled);
      sqlite3_free(titleBooleanCompiled);
      if (rc != SQLITE_OK) {
        sqlite3_free(outJson);
        sqlite3_result_error_nomem(context);
        return;
      }
      sqlite3_result_text(context, outJson, nOutJson, sqlite3_free);
      return;
    } else if (modeBoolean) {
      if (modeBooleanMixed) {
        if (modeBooleanNot) {
          rc = execute_query_build_mixed_not_boolean_compiled(&booleanClauseA, &booleanClauseB, 0, &compiledExpr, &nCompiledExpr);
        } else {
          rc = execute_query_build_mixed_boolean_compiled(&booleanClauseA, opAnd, &booleanClauseB, 0, &compiledExpr, &nCompiledExpr);
        }
      } else {
        rc = execute_query_build_simple_boolean_compiled_for_field(
            &tokenA, &tokenB, opAnd, modeBooleanNot, 0, &compiledExpr, &nCompiledExpr);
      }
    } else if (modePhrase) {
      rc = execute_query_build_phrase_compiled(phraseTerms, phraseTermCount, phraseSlop, &compiledExpr, &nCompiledExpr);
    } else {
      rc = execute_query_build_simple_token_compiled_for_field(&tokenA, 0, &compiledExpr, &nCompiledExpr);
    }
    if (rc != SQLITE_OK) {
      execute_query_free_parsed_token_clause(&tokenA);
      execute_query_free_parsed_token_clause(&tokenB);
      execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
      execute_query_free_boolean_clause(&booleanClauseA);
      execute_query_free_boolean_clause(&booleanClauseB);
      sqlite3_free(compiledExpr);
      sqlite3_result_error_nomem(context);
      return;
    }
    pageCompiled = compiledExpr;
    nPageCompiled = nCompiledExpr;
    titleCompiled = compiledExpr;
    nTitleCompiled = nCompiledExpr;
  }

  ExecuteQueryHit *pageHits = NULL;
  int pageHitCount = 0;
  ExecuteQueryHit *titleHits = NULL;
  int titleHitCount = 0;
  int mixedBooleanHasScopedClauses =
      (modeBoolean && modeBooleanMixed && (booleanClauseA.scopeFieldKind != 0 || booleanClauseB.scopeFieldKind != 0));

  if (modeBoolean && modeBooleanMixed && mixedBooleanHasScopedClauses) {
    if (modeBooleanNot) {
      rc = execute_query_build_mixed_not_boolean_compiled(&booleanClauseA, &booleanClauseB, 1, &pageBooleanCompiled, &nPageBooleanCompiled);
    } else {
      rc = execute_query_build_mixed_boolean_compiled(&booleanClauseA, opAnd, &booleanClauseB, 1, &pageBooleanCompiled, &nPageBooleanCompiled);
    }
    if (rc == SQLITE_OK) {
      if (modeBooleanNot) {
        rc = execute_query_build_mixed_not_boolean_compiled(
            &booleanClauseA, &booleanClauseB, 2, &titleBooleanCompiled, &nTitleBooleanCompiled);
      } else {
        rc = execute_query_build_mixed_boolean_compiled(
            &booleanClauseA, opAnd, &booleanClauseB, 2, &titleBooleanCompiled, &nTitleBooleanCompiled);
      }
    }
    if (rc != SQLITE_OK) {
      execute_query_free_parsed_token_clause(&tokenA);
      execute_query_free_parsed_token_clause(&tokenB);
      execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
      execute_query_free_boolean_clause(&booleanClauseA);
      execute_query_free_boolean_clause(&booleanClauseB);
      sqlite3_free(compiledExpr);
      sqlite3_free(pageBooleanCompiled);
      sqlite3_free(titleBooleanCompiled);
      sqlite3_result_error_nomem(context);
      return;
    }
    pageCompiled = pageBooleanCompiled;
    nPageCompiled = nPageBooleanCompiled;
    titleCompiled = titleBooleanCompiled;
    nTitleCompiled = nTitleBooleanCompiled;
  } else if (modeBoolean && (!modeBooleanMixed) && booleanHasScopedTokens) {
    rc = execute_query_build_simple_boolean_compiled_for_field(
        &tokenA, &tokenB, opAnd, modeBooleanNot, 1, &pageBooleanCompiled, &nPageBooleanCompiled);
    if (rc == SQLITE_OK) {
      rc = execute_query_build_simple_boolean_compiled_for_field(
          &tokenA, &tokenB, opAnd, modeBooleanNot, 2, &titleBooleanCompiled, &nTitleBooleanCompiled);
    }
    if (rc != SQLITE_OK) {
      execute_query_free_parsed_token_clause(&tokenA);
      execute_query_free_parsed_token_clause(&tokenB);
      execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
      execute_query_free_boolean_clause(&booleanClauseA);
      execute_query_free_boolean_clause(&booleanClauseB);
      sqlite3_free(compiledExpr);
      sqlite3_free(pageBooleanCompiled);
      sqlite3_free(titleBooleanCompiled);
      sqlite3_result_error_nomem(context);
      return;
    }
    pageCompiled = pageBooleanCompiled;
    nPageCompiled = nPageBooleanCompiled;
    titleCompiled = titleBooleanCompiled;
    nTitleCompiled = nTitleBooleanCompiled;
  }
  if (wrapForcedScopeCompiled && forcedScopeFieldKind != 0) {
    if (forcedScopeFieldKind == 1) {
      rc = execute_query_wrap_compiled_expression(pageCompiled, nPageCompiled, &pageBooleanCompiled, &nPageBooleanCompiled);
      if (rc != SQLITE_OK) {
        execute_query_free_parsed_token_clause(&tokenA);
        execute_query_free_parsed_token_clause(&tokenB);
        execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
        execute_query_free_boolean_clause(&booleanClauseA);
        execute_query_free_boolean_clause(&booleanClauseB);
        sqlite3_free(compiledExpr);
        sqlite3_free(pageBooleanCompiled);
        sqlite3_free(titleBooleanCompiled);
        sqlite3_result_error_nomem(context);
        return;
      }
      pageCompiled = pageBooleanCompiled;
      nPageCompiled = nPageBooleanCompiled;
    } else if (forcedScopeFieldKind == 2) {
      rc = execute_query_wrap_compiled_expression(titleCompiled, nTitleCompiled, &titleBooleanCompiled, &nTitleBooleanCompiled);
      if (rc != SQLITE_OK) {
        execute_query_free_parsed_token_clause(&tokenA);
        execute_query_free_parsed_token_clause(&tokenB);
        execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
        execute_query_free_boolean_clause(&booleanClauseA);
        execute_query_free_boolean_clause(&booleanClauseB);
        sqlite3_free(compiledExpr);
        sqlite3_free(pageBooleanCompiled);
        sqlite3_free(titleBooleanCompiled);
        sqlite3_result_error_nomem(context);
        return;
      }
      titleCompiled = titleBooleanCompiled;
      nTitleCompiled = nTitleBooleanCompiled;
    }
  }

  int runPage = 0;
  int runTitle = 0;
  if (modeRecursiveBm25) {
    runPage = (requestedFieldKind == 1 || requestedFieldKind == 3);
    runTitle = (requestedFieldKind == 2 || requestedFieldKind == 3);
  } else {
    int scopeFieldKind = modeBoolean ? 0 : (modePhrase ? phraseScopeFieldKind : (modeTokenPattern ? tokenPattern.scopeFieldKind : tokenA.scopeFieldKind));
    if (forcedScopeFieldKind != 0) {
      scopeFieldKind = forcedScopeFieldKind;
    }
    runPage = ((requestedFieldKind == 1 || requestedFieldKind == 3) && (scopeFieldKind == 0 || scopeFieldKind == 1));
    runTitle = ((requestedFieldKind == 2 || requestedFieldKind == 3) && (scopeFieldKind == 0 || scopeFieldKind == 2));
    if (!runPage && (requestedFieldKind == 1 || requestedFieldKind == 3)) {
      if (!(modeBoolean && (((!modeBooleanMixed) && booleanHasScopedTokens) || mixedBooleanHasScopedClauses))) {
        pageCompiled = "(\"__nohit__\")";
        nPageCompiled = 13;
      }
    }
    if (!runTitle && (requestedFieldKind == 2 || requestedFieldKind == 3)) {
      if (!(modeBoolean && (((!modeBooleanMixed) && booleanHasScopedTokens) || mixedBooleanHasScopedClauses))) {
        titleCompiled = "(\"__nohit__\")";
        nTitleCompiled = 13;
      }
    }
  }

  if (runPage) {
    if (modeRecursiveBm25) {
      scoresAreF32 = 0;
      rc = execute_query_collect_bm25_hits(db, 1, pageCompiled, nPageCompiled, limit, &pageHits, &pageHitCount);
      if (rc == SQLITE_OK) {
        rc = execute_query_apply_recursive_token_boosts(db, 1, query, nQuery, pageHits, pageHitCount);
      }
    } else if (modeTokenPattern) {
      if (tokenPattern.hasWildcard) {
        scoresAreF32 = 0;
        rc = execute_query_collect_constant_hits(db, 1, pageCompiled, nPageCompiled, limit, &pageHits, &pageHitCount);
      } else {
        scoresAreF32 = 0;
        rc = execute_query_collect_bm25_hits(db, 1, pageCompiled, nPageCompiled, limit, &pageHits, &pageHitCount);
      }
      if (rc == SQLITE_OK) {
        execute_query_apply_boost_inplace(pageHits, pageHitCount, tokenPattern.boost);
      }
    } else if (modeBoolean) {
      if (modeBooleanMixed) {
        if (modeBooleanNot) {
          scoresAreF32 = 0;
          rc = execute_query_collect_bm25_hits(db, 1, pageCompiled, nPageCompiled, limit, &pageHits, &pageHitCount);
        } else {
          ExecuteQueryHit *leftHits = NULL;
          int leftCount = 0;
          ExecuteQueryHit *rightHits = NULL;
          int rightCount = 0;
          rc = execute_query_collect_boolean_clause_hits(
              db, 1, &booleanClauseA, EXECUTE_QUERY_BOOLEAN_TERM_LIMIT, &leftHits, &leftCount);
          if (rc == SQLITE_OK) {
            rc = execute_query_collect_boolean_clause_hits(
                db, 1, &booleanClauseB, EXECUTE_QUERY_BOOLEAN_TERM_LIMIT, &rightHits, &rightCount);
          }
          if (rc == SQLITE_OK) {
            rc = execute_query_merge_boolean_hits(1, opAnd, leftHits, leftCount, rightHits, rightCount, limit, &pageHits, &pageHitCount);
          }
          sqlite3_free(leftHits);
          sqlite3_free(rightHits);
        }
      } else if (modeBooleanNot || booleanHasScopedTokens || forceBm25Execution) {
        scoresAreF32 = 0;
        rc = execute_query_collect_bm25_hits(db, 1, pageCompiled, nPageCompiled, limit, &pageHits, &pageHitCount);
        if (rc == SQLITE_OK) {
          rc = execute_query_apply_scoped_boolean_token_boosts(db, 1, &tokenA, &tokenB, pageHits, pageHitCount);
        }
      } else {
        rc = execute_query_score_simple_boolean_field(db, 1, &tokenA, &tokenB, opAnd, limit, &pageHits, &pageHitCount);
      }
    } else if (modePhrase) {
      if (phraseScopeFieldKind != 0) {
        scoresAreF32 = 0;
        rc = execute_query_collect_bm25_hits(db, 1, compiledExpr, nCompiledExpr, limit, &pageHits, &pageHitCount);
      } else {
        rc = execute_query_collect_phrase_hits(db, 1, phraseTerms, phraseTermCount, phraseSlop, limit, &pageHits, &pageHitCount);
      }
      if (rc == SQLITE_OK) {
        execute_query_apply_boost_inplace(pageHits, pageHitCount, phraseBoost);
        if (fabs((double)(phraseBoost - 1.0f)) >= 1e-12) {
          scoresAreF32 = 1;
        }
      }
    } else if (tokenA.analyzedCount == 1 && tokenA.analyzedTerm != NULL && tokenA.nAnalyzedTerm > 0) {
      rc = execute_query_collect_field_hits(db, 1, tokenA.analyzedTerm, tokenA.nAnalyzedTerm, limit, &pageHits, &pageHitCount);
      if (rc == SQLITE_OK) {
        execute_query_apply_boost_inplace(pageHits, pageHitCount, tokenA.boost);
      }
    } else {
      rc = SQLITE_OK;
    }
    if (rc == SQLITE_OK && fabs((double)(topLevelBoost - 1.0f)) >= 1e-12) {
      execute_query_apply_boost_inplace(pageHits, pageHitCount, topLevelBoost);
      scoresAreF32 = 1;
    }
    if (rc != SQLITE_OK) {
      execute_query_free_parsed_token_clause(&tokenA);
      execute_query_free_parsed_token_clause(&tokenB);
      execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
      execute_query_free_boolean_clause(&booleanClauseA);
      execute_query_free_boolean_clause(&booleanClauseB);
      sqlite3_free(compiledExpr);
      sqlite3_free(pageBooleanCompiled);
      sqlite3_free(titleBooleanCompiled);
      sqlite3_free(pageHits);
      sqlite3_free(titleHits);
      sqlite3_result_error(context, sqlite3_errmsg(db), -1);
      return;
    }
  }
  if (runTitle) {
    if (modeRecursiveBm25) {
      scoresAreF32 = 0;
      rc = execute_query_collect_bm25_hits(db, 2, titleCompiled, nTitleCompiled, limit, &titleHits, &titleHitCount);
      if (rc == SQLITE_OK) {
        rc = execute_query_apply_recursive_token_boosts(db, 2, query, nQuery, titleHits, titleHitCount);
      }
    } else if (modeTokenPattern) {
      if (tokenPattern.hasWildcard) {
        scoresAreF32 = 0;
        rc = execute_query_collect_constant_hits(db, 2, titleCompiled, nTitleCompiled, limit, &titleHits, &titleHitCount);
      } else {
        scoresAreF32 = 0;
        rc = execute_query_collect_bm25_hits(db, 2, titleCompiled, nTitleCompiled, limit, &titleHits, &titleHitCount);
      }
      if (rc == SQLITE_OK) {
        execute_query_apply_boost_inplace(titleHits, titleHitCount, tokenPattern.boost);
      }
    } else if (modeBoolean) {
      if (modeBooleanMixed) {
        if (modeBooleanNot) {
          scoresAreF32 = 0;
          rc = execute_query_collect_bm25_hits(db, 2, titleCompiled, nTitleCompiled, limit, &titleHits, &titleHitCount);
        } else {
          ExecuteQueryHit *leftHits = NULL;
          int leftCount = 0;
          ExecuteQueryHit *rightHits = NULL;
          int rightCount = 0;
          rc = execute_query_collect_boolean_clause_hits(
              db, 2, &booleanClauseA, EXECUTE_QUERY_BOOLEAN_TERM_LIMIT, &leftHits, &leftCount);
          if (rc == SQLITE_OK) {
            rc = execute_query_collect_boolean_clause_hits(
                db, 2, &booleanClauseB, EXECUTE_QUERY_BOOLEAN_TERM_LIMIT, &rightHits, &rightCount);
          }
          if (rc == SQLITE_OK) {
            rc = execute_query_merge_boolean_hits(2, opAnd, leftHits, leftCount, rightHits, rightCount, limit, &titleHits, &titleHitCount);
          }
          sqlite3_free(leftHits);
          sqlite3_free(rightHits);
        }
      } else if (modeBooleanNot || booleanHasScopedTokens || forceBm25Execution) {
        scoresAreF32 = 0;
        rc = execute_query_collect_bm25_hits(db, 2, titleCompiled, nTitleCompiled, limit, &titleHits, &titleHitCount);
        if (rc == SQLITE_OK) {
          rc = execute_query_apply_scoped_boolean_token_boosts(db, 2, &tokenA, &tokenB, titleHits, titleHitCount);
        }
      } else {
        rc = execute_query_score_simple_boolean_field(db, 2, &tokenA, &tokenB, opAnd, limit, &titleHits, &titleHitCount);
      }
    } else if (modePhrase) {
      if (phraseScopeFieldKind != 0) {
        scoresAreF32 = 0;
        rc = execute_query_collect_bm25_hits(db, 2, compiledExpr, nCompiledExpr, limit, &titleHits, &titleHitCount);
      } else {
        rc = execute_query_collect_phrase_hits(db, 2, phraseTerms, phraseTermCount, phraseSlop, limit, &titleHits, &titleHitCount);
      }
      if (rc == SQLITE_OK) {
        execute_query_apply_boost_inplace(titleHits, titleHitCount, phraseBoost);
        if (fabs((double)(phraseBoost - 1.0f)) >= 1e-12) {
          scoresAreF32 = 1;
        }
      }
    } else if (tokenA.analyzedCount == 1 && tokenA.analyzedTerm != NULL && tokenA.nAnalyzedTerm > 0) {
      rc = execute_query_collect_field_hits(db, 2, tokenA.analyzedTerm, tokenA.nAnalyzedTerm, limit, &titleHits, &titleHitCount);
      if (rc == SQLITE_OK) {
        execute_query_apply_boost_inplace(titleHits, titleHitCount, tokenA.boost);
      }
    } else {
      rc = SQLITE_OK;
    }
    if (rc == SQLITE_OK && fabs((double)(topLevelBoost - 1.0f)) >= 1e-12) {
      execute_query_apply_boost_inplace(titleHits, titleHitCount, topLevelBoost);
      scoresAreF32 = 1;
    }
    if (rc != SQLITE_OK) {
      execute_query_free_parsed_token_clause(&tokenA);
      execute_query_free_parsed_token_clause(&tokenB);
      execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
      execute_query_free_boolean_clause(&booleanClauseA);
      execute_query_free_boolean_clause(&booleanClauseB);
      sqlite3_free(compiledExpr);
      sqlite3_free(pageBooleanCompiled);
      sqlite3_free(titleBooleanCompiled);
      sqlite3_free(pageHits);
      sqlite3_free(titleHits);
      sqlite3_result_error(context, sqlite3_errmsg(db), -1);
      return;
    }
  }
  execute_query_free_parsed_token_clause(&tokenA);
  execute_query_free_parsed_token_clause(&tokenB);
  execute_query_free_phrase_terms(phraseTerms, phraseTermCount);
  execute_query_free_boolean_clause(&booleanClauseA);
  execute_query_free_boolean_clause(&booleanClauseB);

  ExecuteQueryHit *merged = NULL;
  int mergedCount = 0;
  int mergedCap = 0;
  for (int i = 0; i < pageHitCount; i++) {
    rc = execute_query_hits_append(&merged, &mergedCount, &mergedCap, pageHits[i].fieldKind, pageHits[i].bookId, pageHits[i].itemId, pageHits[i].score);
    if (rc != SQLITE_OK) {
      break;
    }
  }
  for (int i = 0; rc == SQLITE_OK && i < titleHitCount; i++) {
    rc = execute_query_hits_append(
        &merged, &mergedCount, &mergedCap, titleHits[i].fieldKind, titleHits[i].bookId, titleHits[i].itemId, titleHits[i].score);
  }
  sqlite3_free(pageHits);
  sqlite3_free(titleHits);
  if (rc != SQLITE_OK) {
    sqlite3_free(merged);
    sqlite3_free(compiledExpr);
    sqlite3_free(pageBooleanCompiled);
    sqlite3_free(titleBooleanCompiled);
    sqlite3_result_error_nomem(context);
    return;
  }
  if (mergedCount > 1 && requestedFieldKind == 3) {
    qsort(merged, (size_t)mergedCount, sizeof(ExecuteQueryHit), execute_query_hit_compare_merged);
  } else if (mergedCount > 1) {
    qsort(merged, (size_t)mergedCount, sizeof(ExecuteQueryHit), execute_query_hit_compare_field);
  }
  if (mergedCount > limit) {
    mergedCount = limit;
  }

  char *outJson = NULL;
  int nOutJson = 0;
  rc = execute_query_build_payload(
      requestedFieldKind,
      pageCompiled,
      nPageCompiled,
      titleCompiled,
      nTitleCompiled,
      merged,
      mergedCount,
      scoresAreF32,
      &outJson,
      &nOutJson);
  sqlite3_free(merged);
  sqlite3_free(compiledExpr);
  sqlite3_free(pageBooleanCompiled);
  sqlite3_free(titleBooleanCompiled);
  if (rc != SQLITE_OK) {
    sqlite3_free(outJson);
    sqlite3_result_error_nomem(context);
    return;
  }
  sqlite3_result_text(context, outJson, nOutJson, sqlite3_free);
}

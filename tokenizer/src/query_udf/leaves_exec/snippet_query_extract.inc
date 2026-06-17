typedef struct SnippetQueryStringList {
  char **items;
  int *itemLens;
  int count;
  int cap;
} SnippetQueryStringList;

static void snippet_query_string_list_free(SnippetQueryStringList *list) {
  if (list == NULL) {
    return;
  }
  if (list->items != NULL) {
    for (int i = 0; i < list->count; i++) {
      sqlite3_free(list->items[i]);
    }
  }
  sqlite3_free(list->items);
  sqlite3_free(list->itemLens);
  list->items = NULL;
  list->itemLens = NULL;
  list->count = 0;
  list->cap = 0;
}

static unsigned char snippet_query_ascii_lower(unsigned char value) {
  if (value >= 'A' && value <= 'Z') {
    return (unsigned char)(value + ('a' - 'A'));
  }
  return value;
}

static int snippet_query_bytes_equal_ascii_ci(const char *left, int nLeft, const char *right, int nRight) {
  if (nLeft != nRight) {
    return 0;
  }
  for (int i = 0; i < nLeft; i++) {
    unsigned char a = snippet_query_ascii_lower((unsigned char)left[i]);
    unsigned char b = snippet_query_ascii_lower((unsigned char)right[i]);
    if (a != b) {
      return 0;
    }
  }
  return 1;
}

static int snippet_query_list_contains_ascii_ci(const SnippetQueryStringList *list, const char *value, int nValue) {
  if (list == NULL || value == NULL || nValue <= 0) {
    return 0;
  }
  for (int i = 0; i < list->count; i++) {
    if (snippet_query_bytes_equal_ascii_ci(list->items[i], list->itemLens[i], value, nValue)) {
      return 1;
    }
  }
  return 0;
}

static int snippet_query_string_list_append_unique(SnippetQueryStringList *list, const char *value, int nValue) {
  if (list == NULL || value == NULL || nValue <= 0) {
    return SQLITE_OK;
  }
  if (snippet_query_list_contains_ascii_ci(list, value, nValue)) {
    return SQLITE_OK;
  }
  if (list->count >= list->cap) {
    int required = 0;
    int rc = ar_checked_int_sum(list->count, 1, &required);
    if (rc != SQLITE_OK) {
      return rc;
    }
    int nextCap = 0;
    rc = ar_grow_capacity(list->cap, required, 16, &nextCap);
    if (rc != SQLITE_OK) {
      return rc;
    }
    char **nextItems = ar_realloc_array(list->items, nextCap, sizeof(char *));
    if (nextItems == NULL) {
      return SQLITE_NOMEM;
    }
    int *nextLens = ar_realloc_array(list->itemLens, nextCap, sizeof(int));
    if (nextLens == NULL) {
      list->items = nextItems;
      return SQLITE_NOMEM;
    }
    list->items = nextItems;
    list->itemLens = nextLens;
    list->cap = nextCap;
  }
  int copyCap = 0;
  if (ar_checked_int_sum(nValue, 1, &copyCap) != SQLITE_OK) {
    return SQLITE_TOOBIG;
  }
  char *copy = sqlite3_malloc64((sqlite3_uint64)copyCap);
  if (copy == NULL) {
    return SQLITE_NOMEM;
  }
  memcpy(copy, value, (size_t)nValue);
  copy[nValue] = '\0';
  list->items[list->count] = copy;
  list->itemLens[list->count] = nValue;
  list->count++;
  return SQLITE_OK;
}

static int snippet_query_is_token_codepoint(uint32_t cp) {
  if ((cp >= '0' && cp <= '9') || (cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z')) {
    return 1;
  }
  if ((cp >= 0x0621 && cp <= 0x064A) || (cp >= 0x0660 && cp <= 0x0669) || (cp >= 0x06F0 && cp <= 0x06F9)) {
    return 1;
  }
  return 0;
}

static int snippet_query_is_digit_codepoint(uint32_t cp) {
  return (cp >= '0' && cp <= '9') || (cp >= 0x0660 && cp <= 0x0669) || (cp >= 0x06F0 && cp <= 0x06F9);
}

static int snippet_query_is_token_all_digits(const char *token, int nToken) {
  int idx = 0;
  int seen = 0;
  while (idx < nToken) {
    uint32_t cp = 0;
    int consumed = 0;
    if (!utf8_decode(token + idx, nToken - idx, &consumed, &cp) || consumed <= 0) {
      return 0;
    }
    if (!snippet_query_is_digit_codepoint(cp)) {
      return 0;
    }
    seen = 1;
    idx += consumed;
  }
  return seen;
}

static int snippet_query_collect_token_slice(SnippetQueryStringList *out, const char *token, int nToken) {
  if (token == NULL || nToken <= 0) {
    return SQLITE_OK;
  }
  if (snippet_query_bytes_equal_ascii_ci(token, nToken, "AND", 3) || snippet_query_bytes_equal_ascii_ci(token, nToken, "OR", 2) ||
      snippet_query_bytes_equal_ascii_ci(token, nToken, "NOT", 3)) {
    return SQLITE_OK;
  }
  if (snippet_query_bytes_equal_ascii_ci(token, nToken, "page", 4) || snippet_query_bytes_equal_ascii_ci(token, nToken, "title", 5)) {
    return SQLITE_OK;
  }
  if (snippet_query_is_token_all_digits(token, nToken)) {
    return SQLITE_OK;
  }
  return snippet_query_string_list_append_unique(out, token, nToken);
}

static int snippet_query_collect_terms_from_text(SnippetQueryStringList *out, const char *text, int nText) {
  if (text == NULL || nText <= 0) {
    return SQLITE_OK;
  }
  int idx = 0;
  int tokenStart = -1;
  while (idx < nText) {
    uint32_t cp = 0;
    int consumed = 0;
    int decoded = utf8_decode(text + idx, nText - idx, &consumed, &cp);
    if (!decoded || consumed <= 0) {
      consumed = 1;
      cp = 0;
    }
    int isToken = decoded ? snippet_query_is_token_codepoint(cp) : 0;
    if (isToken) {
      if (tokenStart < 0) {
        tokenStart = idx;
      }
      idx += consumed;
      continue;
    }
    if (tokenStart >= 0) {
      int rc = snippet_query_collect_token_slice(out, text + tokenStart, idx - tokenStart);
      if (rc != SQLITE_OK) {
        return rc;
      }
      tokenStart = -1;
    }
    idx += consumed;
  }
  if (tokenStart >= 0) {
    return snippet_query_collect_token_slice(out, text + tokenStart, idx - tokenStart);
  }
  return SQLITE_OK;
}

static void snippet_query_strip_scope_prefix(const char *token, int nToken, const char **outText, int *outLen) {
  *outText = token;
  *outLen = nToken;
  if (token == NULL || nToken <= 5) {
    return;
  }
  if (nToken > 5 && token_equals_ascii_ci(token, 5, "page:")) {
    *outText = token + 5;
    *outLen = nToken - 5;
    return;
  }
  if (nToken > 6 && token_equals_ascii_ci(token, 6, "title:")) {
    *outText = token + 6;
    *outLen = nToken - 6;
  }
}

static void snippet_query_strip_boost_suffix(const char *token, int nToken, const char **outText, int *outLen) {
  *outText = token;
  *outLen = nToken;
  if (token == NULL || nToken <= 0) {
    return;
  }
  for (int idx = nToken - 1; idx >= 0; idx--) {
    if (token[idx] != '^') {
      continue;
    }
    if (is_escaped_at(token, 0, idx)) {
      continue;
    }
    int boostEnd = 0;
    if (parse_decimal_number_span(token, idx + 1, nToken, &boostEnd) && boostEnd == nToken) {
      *outText = token;
      *outLen = idx;
      return;
    }
  }
}

static void snippet_query_strip_fuzzy_suffix(const char *token, int nToken, const char **outText, int *outLen) {
  *outText = token;
  *outLen = nToken;
  if (token == NULL || nToken <= 0) {
    return;
  }
  for (int idx = nToken - 1; idx >= 0; idx--) {
    if (token[idx] != '~') {
      continue;
    }
    if (is_escaped_at(token, 0, idx)) {
      continue;
    }
    int nSuffix = nToken - (idx + 1);
    if (nSuffix == 0) {
      *outLen = idx;
      return;
    }
    if (nSuffix == 1 && (token[idx + 1] == '0' || token[idx + 1] == '1' || token[idx + 1] == '2')) {
      *outLen = idx;
      return;
    }
  }
}

static int snippet_query_extract_phrase_payload(const ParsedSimpleAstClause *clause, const char **outText, int *outLen) {
  *outText = NULL;
  *outLen = 0;
  if (clause == NULL) {
    return 0;
  }
  if (clause->nKind == 6 && token_equals_ascii_ci(clause->kind, clause->nKind, "phrase")) {
    *outText = clause->value;
    *outLen = clause->nValue;
    return 1;
  }
  if (clause->nKind == 11 && token_equals_ascii_ci(clause->kind, clause->nKind, "phrase_slop")) {
    int splitAt = find_unescaped_char_span(clause->value, 0, clause->nValue, '\t');
    if (splitAt < 0 || splitAt + 1 >= clause->nValue) {
      return 0;
    }
    *outText = clause->value + splitAt + 1;
    *outLen = clause->nValue - (splitAt + 1);
    return 1;
  }
  if (clause->nKind == 12 && token_equals_ascii_ci(clause->kind, clause->nKind, "field_phrase")) {
    int splitAt = find_unescaped_char_span(clause->value, 0, clause->nValue, ':');
    if (splitAt < 0 || splitAt + 1 >= clause->nValue) {
      return 0;
    }
    *outText = clause->value + splitAt + 1;
    *outLen = clause->nValue - (splitAt + 1);
    return 1;
  }
  if (clause->nKind == 17 && token_equals_ascii_ci(clause->kind, clause->nKind, "field_phrase_slop")) {
    int scopeSplit = find_unescaped_char_span(clause->value, 0, clause->nValue, ':');
    if (scopeSplit < 0 || scopeSplit + 1 >= clause->nValue) {
      return 0;
    }
    int slopSplit = find_unescaped_char_span(clause->value, scopeSplit + 1, clause->nValue, '\t');
    if (slopSplit < 0 || slopSplit + 1 >= clause->nValue) {
      return 0;
    }
    *outText = clause->value + slopSplit + 1;
    *outLen = clause->nValue - (slopSplit + 1);
    return 1;
  }
  return 0;
}

static int snippet_query_extract_field_group_inner(const ParsedSimpleAstClause *clause, const char **outText, int *outLen) {
  *outText = NULL;
  *outLen = 0;
  if (clause == NULL) {
    return 0;
  }
  if (clause->nKind == 11 && token_equals_ascii_ci(clause->kind, clause->nKind, "field_group")) {
    int splitAt = find_unescaped_char_span(clause->value, 0, clause->nValue, '\t');
    if (splitAt < 0 || splitAt + 1 >= clause->nValue) {
      return 0;
    }
    *outText = clause->value + splitAt + 1;
    *outLen = clause->nValue - (splitAt + 1);
    return 1;
  }
  if (clause->nKind == 17 && token_equals_ascii_ci(clause->kind, clause->nKind, "field_group_boost")) {
    int firstSplit = find_unescaped_char_span(clause->value, 0, clause->nValue, '\t');
    if (firstSplit < 0 || firstSplit + 1 >= clause->nValue) {
      return 0;
    }
    int secondSplit = find_unescaped_char_span(clause->value, firstSplit + 1, clause->nValue, '\t');
    if (secondSplit < 0 || secondSplit + 1 >= clause->nValue) {
      return 0;
    }
    *outText = clause->value + secondSplit + 1;
    *outLen = clause->nValue - (secondSplit + 1);
    return 1;
  }
  return 0;
}

static int snippet_query_escape_phrase(const char *text, int nText, char **outText, int *outLen) {
  *outText = NULL;
  *outLen = 0;
  if (text == NULL || nText <= 0) {
    return SQLITE_OK;
  }
  int outCap = 0;
  if (ar_checked_int_sum((sqlite3_int64)nText * 2, 1, &outCap) != SQLITE_OK) {
    return SQLITE_TOOBIG;
  }
  char *out = sqlite3_malloc64((sqlite3_uint64)outCap);
  if (out == NULL) {
    return SQLITE_NOMEM;
  }
  int pos = 0;
  for (int i = 0; i < nText; i++) {
    if (text[i] == '\\' || text[i] == '"') {
      out[pos++] = '\\';
    }
    out[pos++] = text[i];
  }
  out[pos] = '\0';
  *outText = out;
  *outLen = pos;
  return SQLITE_OK;
}

static int snippet_query_collect_from_clause_recursive(
    const ParsedSimpleAstClause *clause,
    SnippetQueryStringList *termOut,
    SnippetQueryStringList *phraseOut,
    int depth);

static int snippet_query_collect_from_query_recursive(
    const char *query,
    int nQuery,
    SnippetQueryStringList *termOut,
    SnippetQueryStringList *phraseOut,
    int depth) {
  if (query == NULL || nQuery <= 0) {
    return SQLITE_OK;
  }
  if (depth > 16) {
    return SQLITE_ERROR;
  }
  ParsedSimpleAstClause clauseA;
  ParsedSimpleAstClause clauseB;
  int clauseCount = 0;
  if (!parse_simple_query_ast_clauses(query, nQuery, &clauseA, &clauseB, &clauseCount)) {
    return SQLITE_ERROR;
  }
  if (clauseCount >= 1) {
    int rc = snippet_query_collect_from_clause_recursive(&clauseA, termOut, phraseOut, depth + 1);
    if (rc != SQLITE_OK) {
      return rc;
    }
  }
  if (clauseCount >= 2) {
    int rc = snippet_query_collect_from_clause_recursive(&clauseB, termOut, phraseOut, depth + 1);
    if (rc != SQLITE_OK) {
      return rc;
    }
  }
  return SQLITE_OK;
}

static int snippet_query_collect_from_clause_recursive(
    const ParsedSimpleAstClause *clause,
    SnippetQueryStringList *termOut,
    SnippetQueryStringList *phraseOut,
    int depth) {
  if (clause == NULL) {
    return SQLITE_OK;
  }
  if (depth > 16) {
    return SQLITE_ERROR;
  }

  if (clause->nKind == 5 && token_equals_ascii_ci(clause->kind, clause->nKind, "group")) {
    return snippet_query_collect_from_query_recursive(clause->value, clause->nValue, termOut, phraseOut, depth + 1);
  }

  const char *innerQuery = NULL;
  int nInnerQuery = 0;
  if (snippet_query_extract_field_group_inner(clause, &innerQuery, &nInnerQuery)) {
    return snippet_query_collect_from_query_recursive(innerQuery, nInnerQuery, termOut, phraseOut, depth + 1);
  }

  if (termOut != NULL && clause->nKind == 5 && token_equals_ascii_ci(clause->kind, clause->nKind, "token")) {
    const char *payload = clause->value;
    int nPayload = clause->nValue;
    snippet_query_strip_scope_prefix(payload, nPayload, &payload, &nPayload);
    snippet_query_strip_boost_suffix(payload, nPayload, &payload, &nPayload);
    snippet_query_strip_fuzzy_suffix(payload, nPayload, &payload, &nPayload);
    int rc = snippet_query_collect_terms_from_text(termOut, payload, nPayload);
    if (rc != SQLITE_OK) {
      return rc;
    }
  }

  const char *phrasePayload = NULL;
  int nPhrasePayload = 0;
  if (!snippet_query_extract_phrase_payload(clause, &phrasePayload, &nPhrasePayload)) {
    return SQLITE_OK;
  }

  if (termOut != NULL) {
    int rc = snippet_query_collect_terms_from_text(termOut, phrasePayload, nPhrasePayload);
    if (rc != SQLITE_OK) {
      return rc;
    }
  }
  if (phraseOut != NULL) {
    int start = 0;
    int end = 0;
    trim_ascii_space_span(phrasePayload, nPhrasePayload, &start, &end);
    if (start >= end) {
      return SQLITE_OK;
    }
    char *escaped = NULL;
    int nEscaped = 0;
    int rc = snippet_query_escape_phrase(phrasePayload + start, end - start, &escaped, &nEscaped);
    if (rc != SQLITE_OK) {
      return rc;
    }
    rc = snippet_query_string_list_append_unique(phraseOut, escaped, nEscaped);
    sqlite3_free(escaped);
    if (rc != SQLITE_OK) {
      return rc;
    }
  }
  return SQLITE_OK;
}

static int snippet_query_list_to_json(const SnippetQueryStringList *list, char **jsonOut, int *nJsonOut) {
  *jsonOut = NULL;
  *nJsonOut = 0;
  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_char(&json, &jsonLen, &jsonCap, '[');
  for (int i = 0; rc == SQLITE_OK && list != NULL && i < list->count; i++) {
    if (i > 0) {
      rc = json_builder_append_char(&json, &jsonLen, &jsonCap, ',');
      if (rc != SQLITE_OK) {
        break;
      }
    }
    rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, list->items[i], list->itemLens[i]);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&json, &jsonLen, &jsonCap, ']');
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    return rc;
  }
  json[jsonLen] = '\0';
  *jsonOut = json;
  *nJsonOut = jsonLen;
  return SQLITE_OK;
}

static void udf_sqlite_tokenizer_ar_extract_snippet_query_terms_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_extract_snippet_query_terms_json expects 1 argument", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }
  const char *query = (const char *)sqlite3_value_text(argv[0]);
  int nQuery = sqlite3_value_bytes(argv[0]);
  if (query == NULL || nQuery <= 0) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }

  SnippetQueryStringList terms;
  memset(&terms, 0, sizeof(terms));
  int rc = snippet_query_collect_from_query_recursive(query, nQuery, &terms, NULL, 0);
  if (rc == SQLITE_ERROR) {
    snippet_query_string_list_free(&terms);
    sqlite3_result_error(
        context,
        "C snippet extraction backend is not available: unsupported query shape for current C parser rollout",
        -1);
    return;
  }
  if (rc != SQLITE_OK) {
    snippet_query_string_list_free(&terms);
    sqlite3_result_error_nomem(context);
    return;
  }

  char *json = NULL;
  int nJson = 0;
  rc = snippet_query_list_to_json(&terms, &json, &nJson);
  snippet_query_string_list_free(&terms);
  if (rc != SQLITE_OK) {
    sqlite3_result_error_nomem(context);
    return;
  }
  sqlite3_result_text(context, json, nJson, sqlite3_free);
}

static void udf_sqlite_tokenizer_ar_extract_snippet_query_phrases_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_extract_snippet_query_phrases_json expects 1 argument", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }
  const char *query = (const char *)sqlite3_value_text(argv[0]);
  int nQuery = sqlite3_value_bytes(argv[0]);
  if (query == NULL || nQuery <= 0) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }

  SnippetQueryStringList phrases;
  memset(&phrases, 0, sizeof(phrases));
  int rc = snippet_query_collect_from_query_recursive(query, nQuery, NULL, &phrases, 0);
  if (rc == SQLITE_ERROR) {
    snippet_query_string_list_free(&phrases);
    sqlite3_result_error(
        context,
        "C snippet extraction backend is not available: unsupported query shape for current C parser rollout",
        -1);
    return;
  }
  if (rc != SQLITE_OK) {
    snippet_query_string_list_free(&phrases);
    sqlite3_result_error_nomem(context);
    return;
  }

  char *json = NULL;
  int nJson = 0;
  rc = snippet_query_list_to_json(&phrases, &json, &nJson);
  snippet_query_string_list_free(&phrases);
  if (rc != SQLITE_OK) {
    sqlite3_result_error_nomem(context);
    return;
  }
  sqlite3_result_text(context, json, nJson, sqlite3_free);
}

typedef struct SnippetQueryValueSpans {
  SnippetSpan *spans;
  int count;
} SnippetQueryValueSpans;

static void snippet_query_value_spans_free(SnippetQueryValueSpans *items, int count) {
  if (items == NULL) {
    return;
  }
  for (int i = 0; i < count; i++) {
    sqlite3_free(items[i].spans);
  }
  sqlite3_free(items);
}

static int snippet_query_find_all_spans(
    const char *text,
    int nText,
    const char *term,
    int nTerm,
    int limit,
    SnippetSpan **outSpans,
    int *outCount) {
  *outSpans = NULL;
  *outCount = 0;
  if (text == NULL || nText <= 0 || term == NULL || nTerm <= 0 || limit <= 0) {
    return SQLITE_OK;
  }

  NormalizeOptions options = {
      .ignoreDiacritics = 1,
      .ignoreHamzaForms = 1,
      .ignoreLetterForms = 1,
      .ignoreDigitForms = 1,
      .lowercase = 1,
      .keepWildcards = 0,
  };

  uint32_t *textCodepoints = NULL;
  int *textOrigIndexMap = NULL;
  int textCount = 0;
  int rc = normalize_to_codepoints_with_map(text, nText, &options, &textCodepoints, &textOrigIndexMap, &textCount);
  if (rc != SQLITE_OK) {
    return rc;
  }

  uint32_t *termCodepoints = NULL;
  int *termMapUnused = NULL;
  int termCount = 0;
  rc = normalize_to_codepoints_with_map(term, nTerm, &options, &termCodepoints, &termMapUnused, &termCount);
  sqlite3_free(termMapUnused);
  if (rc != SQLITE_OK) {
    sqlite3_free(textCodepoints);
    sqlite3_free(textOrigIndexMap);
    return rc;
  }

  if (textCount <= 0 || termCount <= 0 || termCount > textCount) {
    sqlite3_free(textCodepoints);
    sqlite3_free(textOrigIndexMap);
    sqlite3_free(termCodepoints);
    return SQLITE_OK;
  }

  int cap = 8;
  int count = 0;
  SnippetSpan *spans = ar_malloc_array(cap, sizeof(SnippetSpan));
  if (spans == NULL) {
    sqlite3_free(textCodepoints);
    sqlite3_free(textOrigIndexMap);
    sqlite3_free(termCodepoints);
    return SQLITE_NOMEM;
  }

  int step = (termCount > 0) ? termCount : 1;
  int cursor = 0;
  while (count < limit && cursor < textCount) {
    int pos = find_codepoint_subsequence(textCodepoints, textCount, termCodepoints, termCount, cursor);
    if (pos < 0) {
      break;
    }
    int startOrig = textOrigIndexMap[pos];
    int endOrig = textOrigIndexMap[pos + termCount - 1] + 1;
    if (endOrig > startOrig) {
      if (count >= cap) {
        int required = 0;
        int growRc = ar_checked_int_sum(count, 1, &required);
        if (growRc != SQLITE_OK) {
          sqlite3_free(spans);
          sqlite3_free(textCodepoints);
          sqlite3_free(textOrigIndexMap);
          sqlite3_free(termCodepoints);
          return growRc;
        }
        int nextCap = 0;
        growRc = ar_grow_capacity(cap, required, 8, &nextCap);
        if (growRc != SQLITE_OK) {
          sqlite3_free(spans);
          sqlite3_free(textCodepoints);
          sqlite3_free(textOrigIndexMap);
          sqlite3_free(termCodepoints);
          return growRc;
        }
        SnippetSpan *next = ar_realloc_array(spans, nextCap, sizeof(SnippetSpan));
        if (next == NULL) {
          sqlite3_free(spans);
          sqlite3_free(textCodepoints);
          sqlite3_free(textOrigIndexMap);
          sqlite3_free(termCodepoints);
          return SQLITE_NOMEM;
        }
        spans = next;
        cap = nextCap;
      }
      spans[count].start = startOrig;
      spans[count].end = endOrig;
      count++;
    }
    cursor = pos + step;
  }

  sqlite3_free(textCodepoints);
  sqlite3_free(textOrigIndexMap);
  sqlite3_free(termCodepoints);

  *outSpans = spans;
  *outCount = count;
  return SQLITE_OK;
}

static int snippet_query_collect_value_spans(
    const char *text,
    int nText,
    const SnippetQueryStringList *values,
    int limit,
    SnippetQueryValueSpans **outItems) {
  *outItems = NULL;
  if (values == NULL || values->count <= 0) {
    return SQLITE_OK;
  }
  SnippetQueryValueSpans *items = ar_malloc_array(values->count, sizeof(SnippetQueryValueSpans));
  if (items == NULL) {
    return SQLITE_NOMEM;
  }
  memset(items, 0, (size_t)values->count * sizeof(SnippetQueryValueSpans));
  for (int i = 0; i < values->count; i++) {
    int rc = snippet_query_find_all_spans(text, nText, values->items[i], values->itemLens[i], limit, &items[i].spans, &items[i].count);
    if (rc != SQLITE_OK) {
      snippet_query_value_spans_free(items, values->count);
      return rc;
    }
  }
  *outItems = items;
  return SQLITE_OK;
}

static int snippet_query_append_span(SnippetSpan **items, int *count, int *cap, int start, int end) {
  if (end <= start) {
    return SQLITE_OK;
  }
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
    SnippetSpan *next = ar_realloc_array(*items, nextCap, sizeof(SnippetSpan));
    if (next == NULL) {
      return SQLITE_NOMEM;
    }
    *items = next;
    *cap = nextCap;
  }
  (*items)[*count].start = start;
  (*items)[*count].end = end;
  (*count)++;
  return SQLITE_OK;
}

static int snippet_query_append_ranked_span(SnippetRankedSpan **items, int *count, int *cap, int start, int end, int priority) {
  if (end <= start) {
    return SQLITE_OK;
  }
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
    SnippetRankedSpan *next = ar_realloc_array(*items, nextCap, sizeof(SnippetRankedSpan));
    if (next == NULL) {
      return SQLITE_NOMEM;
    }
    *items = next;
    *cap = nextCap;
  }
  (*items)[*count].start = start;
  (*items)[*count].end = end;
  (*items)[*count].priority = priority;
  (*count)++;
  return SQLITE_OK;
}

static int snippet_query_window_exists(const SnippetSpan *windows, int windowCount, int start, int end) {
  for (int i = 0; i < windowCount; i++) {
    if (windows[i].start == start && windows[i].end == end) {
      return 1;
    }
  }
  return 0;
}

static void snippet_query_snap_window_from_codepoints(
    const uint32_t *textCodepoints,
    int textLen,
    int start,
    int end,
    int scan,
    int *outStart,
    int *outEnd) {
  int s = start;
  int e = end;
  if (s < 0) {
    s = 0;
  }
  if (e < 0) {
    e = 0;
  }
  if (s > textLen) {
    s = textLen;
  }
  if (e > textLen) {
    e = textLen;
  }
  if (scan < 0) {
    scan = 0;
  }

  int snappedStart = s;
  int leftBound = s - scan;
  if (leftBound < 0) {
    leftBound = 0;
  }
  int i = s;
  while (i > leftBound) {
    if (is_snippet_boundary_codepoint(textCodepoints[i - 1])) {
      snappedStart = i;
      break;
    }
    i--;
  }

  int snappedEnd = e;
  int rightBound = e + scan;
  if (rightBound > textLen) {
    rightBound = textLen;
  }
  i = e;
  while (i < rightBound) {
    if (is_snippet_boundary_codepoint(textCodepoints[i])) {
      snappedEnd = i;
      break;
    }
    i++;
  }
  if (snappedEnd <= snappedStart) {
    snappedStart = s;
    snappedEnd = e;
    if (snappedEnd < snappedStart) {
      snappedEnd = snappedStart;
    }
  }
  *outStart = snappedStart;
  *outEnd = snappedEnd;
}

static int snippet_query_select_non_overlapping(
    SnippetRankedSpan *ranked,
    int rankedCount,
    SnippetSpan **outSpans,
    int *outCount) {
  *outSpans = NULL;
  *outCount = 0;
  if (ranked == NULL || rankedCount <= 0) {
    return SQLITE_OK;
  }
  qsort(ranked, (size_t)rankedCount, sizeof(SnippetRankedSpan), compare_ranked_spans);
  SnippetSpan *picked = ar_malloc_array(rankedCount, sizeof(SnippetSpan));
  if (picked == NULL) {
    return SQLITE_NOMEM;
  }
  int pickedCount = 0;
  for (int i = 0; i < rankedCount; i++) {
    int overlaps = 0;
    for (int j = 0; j < pickedCount; j++) {
      if (spans_overlap_inclusive(ranked[i].start, ranked[i].end, picked[j].start, picked[j].end)) {
        overlaps = 1;
        break;
      }
    }
    if (!overlaps) {
      picked[pickedCount].start = ranked[i].start;
      picked[pickedCount].end = ranked[i].end;
      pickedCount++;
    }
  }
  if (pickedCount > 1) {
    qsort(picked, (size_t)pickedCount, sizeof(SnippetSpan), compare_spans_asc);
  }
  *outSpans = picked;
  *outCount = pickedCount;
  return SQLITE_OK;
}

static int snippet_query_render(
    const uint32_t *textCodepoints,
    int textLen,
    int start,
    int end,
    const SnippetSpan *spans,
    int spanCount,
    char **outText,
    int *outLen) {
  *outText = NULL;
  *outLen = 0;
  if (textCodepoints == NULL || textLen <= 0) {
    return SQLITE_OK;
  }

  int s = start;
  int e = end;
  if (s < 0) {
    s = 0;
  }
  if (e < 0) {
    e = 0;
  }
  if (s > textLen) {
    s = textLen;
  }
  if (e > textLen) {
    e = textLen;
  }
  if (e < s) {
    e = s;
  }

  int snippetLen = e - s;
  int snippetCap = 0;
  if (ar_checked_int_sum(snippetLen, 16, &snippetCap) != SQLITE_OK || snippetCap < 16) {
    return SQLITE_TOOBIG;
  }
  uint32_t *snippet = ar_malloc_array(snippetCap, sizeof(uint32_t));
  if (snippet == NULL) {
    return SQLITE_NOMEM;
  }
  if (snippetLen > 0) {
    memcpy(snippet, textCodepoints + s, (size_t)snippetLen * sizeof(uint32_t));
  }

  SnippetSpan *sortedSpans = NULL;
  if (spanCount > 0) {
    sortedSpans = ar_malloc_array(spanCount, sizeof(SnippetSpan));
    if (sortedSpans == NULL) {
      sqlite3_free(snippet);
      return SQLITE_NOMEM;
    }
    memcpy(sortedSpans, spans, (size_t)spanCount * sizeof(SnippetSpan));
    qsort(sortedSpans, (size_t)spanCount, sizeof(SnippetSpan), compare_spans_desc);
  }

  int rc = SQLITE_OK;
  for (int i = 0; i < spanCount; i++) {
    int relStart = sortedSpans[i].start - s;
    int relEnd = sortedSpans[i].end - s;
    if (relStart < 0) {
      relStart = 0;
    }
    if (relStart > snippetLen) {
      relStart = snippetLen;
    }
    if (relEnd < relStart) {
      relEnd = relStart;
    }
    if (relEnd > snippetLen) {
      relEnd = snippetLen;
    }

    rc = ensure_codepoint_capacity(&snippet, &snippetCap, snippetLen + 6);
    if (rc != SQLITE_OK) {
      break;
    }

    memmove(snippet + relEnd + 2, snippet + relEnd, (size_t)(snippetLen - relEnd) * sizeof(uint32_t));
    snippet[relEnd] = ']';
    snippet[relEnd + 1] = ']';
    snippetLen += 2;

    memmove(snippet + relStart + 2, snippet + relStart, (size_t)(snippetLen - relStart) * sizeof(uint32_t));
    snippet[relStart] = '[';
    snippet[relStart + 1] = '[';
    snippetLen += 2;
  }
  sqlite3_free(sortedSpans);
  if (rc != SQLITE_OK) {
    sqlite3_free(snippet);
    return rc;
  }

  if (s > 0) {
    rc = ensure_codepoint_capacity(&snippet, &snippetCap, snippetLen + 2);
    if (rc != SQLITE_OK) {
      sqlite3_free(snippet);
      return rc;
    }
    memmove(snippet + 1, snippet, (size_t)snippetLen * sizeof(uint32_t));
    snippet[0] = 0x2026;
    snippetLen++;
  }
  if (e < textLen) {
    rc = ensure_codepoint_capacity(&snippet, &snippetCap, snippetLen + 2);
    if (rc != SQLITE_OK) {
      sqlite3_free(snippet);
      return rc;
    }
    snippet[snippetLen++] = 0x2026;
  }

  char *utf8 = NULL;
  int nUtf8 = 0;
  rc = encode_codepoints_to_utf8_text(snippet, snippetLen, &utf8, &nUtf8);
  sqlite3_free(snippet);
  if (rc != SQLITE_OK) {
    return rc;
  }
  *outText = utf8;
  *outLen = nUtf8;
  return SQLITE_OK;
}

static int snippet_query_candidate_is_better(
    int phraseUnique,
    int phraseHits,
    int termUnique,
    int termHits,
    int highlightChars,
    int start,
    int end,
    int bestPhraseUnique,
    int bestPhraseHits,
    int bestTermUnique,
    int bestTermHits,
    int bestHighlightChars,
    int bestStart,
    int bestEnd) {
  if (phraseUnique != bestPhraseUnique) {
    return phraseUnique > bestPhraseUnique;
  }
  if (phraseHits != bestPhraseHits) {
    return phraseHits > bestPhraseHits;
  }
  if (termUnique != bestTermUnique) {
    return termUnique > bestTermUnique;
  }
  if (termHits != bestTermHits) {
    return termHits > bestTermHits;
  }
  if (highlightChars != bestHighlightChars) {
    return highlightChars > bestHighlightChars;
  }
  if (start != bestStart) {
    return start < bestStart;
  }
  int length = end - start;
  int bestLength = bestEnd - bestStart;
  if (length != bestLength) {
    return length < bestLength;
  }
  return 0;
}

static void udf_sqlite_tokenizer_ar_build_field_snippet_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 2) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_build_field_snippet_json expects 2 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }

  const char *text = (const char *)sqlite3_value_text(argv[0]);
  int nText = sqlite3_value_bytes(argv[0]);
  const char *query = (const char *)sqlite3_value_text(argv[1]);
  int nQuery = sqlite3_value_bytes(argv[1]);
  if (text == NULL || nText <= 0 || query == NULL || nQuery <= 0) {
    sqlite3_result_null(context);
    return;
  }

  SnippetQueryStringList terms;
  SnippetQueryStringList phrases;
  memset(&terms, 0, sizeof(terms));
  memset(&phrases, 0, sizeof(phrases));
  int rc = snippet_query_collect_from_query_recursive(query, nQuery, &terms, &phrases, 0);
  if (rc == SQLITE_ERROR) {
    snippet_query_string_list_free(&terms);
    snippet_query_string_list_free(&phrases);
    sqlite3_result_error(
        context,
        "C snippet extraction backend is not available: unsupported query shape for current C parser rollout",
        -1);
    return;
  }
  if (rc != SQLITE_OK) {
    snippet_query_string_list_free(&terms);
    snippet_query_string_list_free(&phrases);
    sqlite3_result_error_nomem(context);
    return;
  }

  SnippetQueryValueSpans *phraseSpans = NULL;
  SnippetQueryValueSpans *termSpans = NULL;
  rc = snippet_query_collect_value_spans(text, nText, &phrases, 8, &phraseSpans);
  if (rc == SQLITE_OK) {
    rc = snippet_query_collect_value_spans(text, nText, &terms, 8, &termSpans);
  }
  if (rc != SQLITE_OK) {
    snippet_query_value_spans_free(phraseSpans, phrases.count);
    snippet_query_value_spans_free(termSpans, terms.count);
    snippet_query_string_list_free(&terms);
    snippet_query_string_list_free(&phrases);
    sqlite3_result_error_nomem(context);
    return;
  }

  uint32_t *textCodepoints = NULL;
  int textLen = 0;
  rc = decode_text_to_codepoints(text, nText, &textCodepoints, &textLen);
  if (rc != SQLITE_OK) {
    snippet_query_value_spans_free(phraseSpans, phrases.count);
    snippet_query_value_spans_free(termSpans, terms.count);
    snippet_query_string_list_free(&terms);
    snippet_query_string_list_free(&phrases);
    sqlite3_result_error_nomem(context);
    return;
  }

  SnippetSpan *anchors = NULL;
  int anchorCount = 0;
  int anchorCap = 0;
  for (int i = 0; i < phrases.count; i++) {
    for (int j = 0; j < phraseSpans[i].count; j++) {
      rc = snippet_query_append_span(&anchors, &anchorCount, &anchorCap, phraseSpans[i].spans[j].start, phraseSpans[i].spans[j].end);
      if (rc != SQLITE_OK) {
        break;
      }
    }
    if (rc != SQLITE_OK) {
      break;
    }
  }
  for (int i = 0; rc == SQLITE_OK && i < terms.count; i++) {
    for (int j = 0; j < termSpans[i].count; j++) {
      rc = snippet_query_append_span(&anchors, &anchorCount, &anchorCap, termSpans[i].spans[j].start, termSpans[i].spans[j].end);
      if (rc != SQLITE_OK) {
        break;
      }
    }
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(anchors);
    sqlite3_free(textCodepoints);
    snippet_query_value_spans_free(phraseSpans, phrases.count);
    snippet_query_value_spans_free(termSpans, terms.count);
    snippet_query_string_list_free(&terms);
    snippet_query_string_list_free(&phrases);
    sqlite3_result_error_nomem(context);
    return;
  }
  if (anchorCount == 0) {
    int fallbackEnd = (textLen > 0) ? 1 : 0;
    rc = snippet_query_append_span(&anchors, &anchorCount, &anchorCap, 0, fallbackEnd);
    if (rc != SQLITE_OK) {
      sqlite3_free(anchors);
      sqlite3_free(textCodepoints);
      snippet_query_value_spans_free(phraseSpans, phrases.count);
      snippet_query_value_spans_free(termSpans, terms.count);
      snippet_query_string_list_free(&terms);
      snippet_query_string_list_free(&phrases);
      sqlite3_result_error_nomem(context);
      return;
    }
  }

  SnippetSpan *seenWindows = NULL;
  int seenWindowCount = 0;
  int seenWindowCap = 0;

  char *bestSnippet = NULL;
  int bestSnippetLen = 0;
  int hasBest = 0;
  int bestPhraseUnique = 0;
  int bestPhraseHits = 0;
  int bestTermUnique = 0;
  int bestTermHits = 0;
  int bestHighlightChars = 0;
  int bestStart = 0;
  int bestEnd = 0;

  for (int anchorIdx = 0; anchorIdx < anchorCount; anchorIdx++) {
    int anchorStart = anchors[anchorIdx].start;
    int anchorEnd = anchors[anchorIdx].end;
    int anchorLen = anchorEnd - anchorStart;
    if (anchorLen < 0) {
      anchorLen = 0;
    }

    int rawStart = anchorStart - 24;
    if (rawStart < 0) {
      rawStart = 0;
    }
    int rawEnd = anchorStart + ((anchorLen > 0) ? (anchorLen + 24) : 48);
    if (rawEnd > textLen) {
      rawEnd = textLen;
    }
    int start = 0;
    int end = 0;
    snippet_query_snap_window_from_codepoints(textCodepoints, textLen, rawStart, rawEnd, 12, &start, &end);
    if (end <= start) {
      continue;
    }
    if (snippet_query_window_exists(seenWindows, seenWindowCount, start, end)) {
      continue;
    }
    rc = snippet_query_append_span(&seenWindows, &seenWindowCount, &seenWindowCap, start, end);
    if (rc != SQLITE_OK) {
      break;
    }

    SnippetRankedSpan *candidateSpans = NULL;
    int candidateCount = 0;
    int candidateCap = 0;
    int phraseUnique = 0;
    int phraseHits = 0;
    int termUnique = 0;
    int termHits = 0;

    for (int i = 0; i < phrases.count; i++) {
      int localHits = 0;
      for (int j = 0; j < phraseSpans[i].count; j++) {
        int spanStart = phraseSpans[i].spans[j].start;
        int spanEnd = phraseSpans[i].spans[j].end;
        if (!spans_overlap_inclusive(spanStart, spanEnd, start, end)) {
          continue;
        }
        localHits++;
        int clippedStart = (spanStart < start) ? start : spanStart;
        int clippedEnd = (spanEnd > end) ? end : spanEnd;
        rc = snippet_query_append_ranked_span(&candidateSpans, &candidateCount, &candidateCap, clippedStart, clippedEnd, 0);
        if (rc != SQLITE_OK) {
          break;
        }
      }
      if (rc != SQLITE_OK) {
        break;
      }
      if (localHits > 0) {
        phraseUnique++;
        phraseHits += localHits;
      }
    }
    for (int i = 0; rc == SQLITE_OK && i < terms.count; i++) {
      int localHits = 0;
      for (int j = 0; j < termSpans[i].count; j++) {
        int spanStart = termSpans[i].spans[j].start;
        int spanEnd = termSpans[i].spans[j].end;
        if (!spans_overlap_inclusive(spanStart, spanEnd, start, end)) {
          continue;
        }
        localHits++;
        int clippedStart = (spanStart < start) ? start : spanStart;
        int clippedEnd = (spanEnd > end) ? end : spanEnd;
        rc = snippet_query_append_ranked_span(&candidateSpans, &candidateCount, &candidateCap, clippedStart, clippedEnd, 1);
        if (rc != SQLITE_OK) {
          break;
        }
      }
      if (localHits > 0) {
        termUnique++;
        termHits += localHits;
      }
    }

    SnippetSpan *highlightSpans = NULL;
    int highlightCount = 0;
    if (rc == SQLITE_OK) {
      rc = snippet_query_select_non_overlapping(candidateSpans, candidateCount, &highlightSpans, &highlightCount);
    }
    sqlite3_free(candidateSpans);
    if (rc != SQLITE_OK) {
      sqlite3_free(highlightSpans);
      break;
    }
    if (highlightCount == 0 && anchorLen > 0) {
      int clippedStart = (anchorStart < start) ? start : anchorStart;
      int clippedEnd = (anchorEnd > end) ? end : anchorEnd;
      if (clippedEnd > clippedStart) {
        SnippetSpan *next = sqlite3_realloc(highlightSpans, sizeof(SnippetSpan));
        if (next == NULL) {
          sqlite3_free(highlightSpans);
          rc = SQLITE_NOMEM;
          break;
        }
        highlightSpans = next;
        highlightSpans[0].start = clippedStart;
        highlightSpans[0].end = clippedEnd;
        highlightCount = 1;
      }
    }

    int highlightChars = 0;
    for (int i = 0; i < highlightCount; i++) {
      int width = highlightSpans[i].end - highlightSpans[i].start;
      if (width > 0) {
        highlightChars += width;
      }
    }

    char *rendered = NULL;
    int renderedLen = 0;
    rc = snippet_query_render(textCodepoints, textLen, start, end, highlightSpans, highlightCount, &rendered, &renderedLen);
    sqlite3_free(highlightSpans);
    if (rc != SQLITE_OK) {
      break;
    }

    int better = 0;
    if (!hasBest) {
      better = 1;
    } else {
      better = snippet_query_candidate_is_better(
          phraseUnique,
          phraseHits,
          termUnique,
          termHits,
          highlightChars,
          start,
          end,
          bestPhraseUnique,
          bestPhraseHits,
          bestTermUnique,
          bestTermHits,
          bestHighlightChars,
          bestStart,
          bestEnd);
    }
    if (better) {
      sqlite3_free(bestSnippet);
      bestSnippet = rendered;
      bestSnippetLen = renderedLen;
      bestPhraseUnique = phraseUnique;
      bestPhraseHits = phraseHits;
      bestTermUnique = termUnique;
      bestTermHits = termHits;
      bestHighlightChars = highlightChars;
      bestStart = start;
      bestEnd = end;
      hasBest = 1;
    } else {
      sqlite3_free(rendered);
    }
  }

  sqlite3_free(seenWindows);
  sqlite3_free(anchors);
  sqlite3_free(textCodepoints);
  snippet_query_value_spans_free(phraseSpans, phrases.count);
  snippet_query_value_spans_free(termSpans, terms.count);
  snippet_query_string_list_free(&terms);
  snippet_query_string_list_free(&phrases);

  if (rc != SQLITE_OK) {
    sqlite3_free(bestSnippet);
    sqlite3_result_error_nomem(context);
    return;
  }
  if (!hasBest || bestSnippet == NULL) {
    sqlite3_free(bestSnippet);
    sqlite3_result_null(context);
    return;
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "{\"snippet\":", 11);
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(&json, &jsonLen, &jsonCap, bestSnippet, bestSnippetLen);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"phrase_unique\":", 17);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_int(&json, &jsonLen, &jsonCap, bestPhraseUnique);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"phrase_hits\":", 15);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_int(&json, &jsonLen, &jsonCap, bestPhraseHits);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"term_unique\":", 15);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_int(&json, &jsonLen, &jsonCap, bestTermUnique);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"term_hits\":", 13);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_int(&json, &jsonLen, &jsonCap, bestTermHits);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"highlight_chars\":", 19);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_int(&json, &jsonLen, &jsonCap, bestHighlightChars);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"start\":", 9);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_int(&json, &jsonLen, &jsonCap, bestStart);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, ",\"length\":", 10);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_int(&json, &jsonLen, &jsonCap, bestEnd - bestStart);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&json, &jsonLen, &jsonCap, '}');
  }
  sqlite3_free(bestSnippet);
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return;
  }
  json[jsonLen] = '\0';
  sqlite3_result_text(context, json, jsonLen, sqlite3_free);
}

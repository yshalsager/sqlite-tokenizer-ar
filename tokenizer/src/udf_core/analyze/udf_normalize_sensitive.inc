static void udf_sqlite_tokenizer_ar_normalize(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc < 1 || argc > 7) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_normalize expects 1..7 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  const char *text = (const char *)sqlite3_value_text(argv[0]);
  int nText = sqlite3_value_bytes(argv[0]);
  if (text == NULL) {
    sqlite3_result_null(context);
    return;
  }

  NormalizeOptions options = {
      .ignoreDiacritics = (argc >= 2) ? sqlite_value_bool(argv[1], 1) : 1,
      .ignoreHamzaForms = (argc >= 3) ? sqlite_value_bool(argv[2], 1) : 1,
      .ignoreLetterForms = (argc >= 4) ? sqlite_value_bool(argv[3], 1) : 1,
      .ignoreDigitForms = (argc >= 5) ? sqlite_value_bool(argv[4], 1) : 1,
      .lowercase = (argc >= 6) ? sqlite_value_bool(argv[5], 0) : 0,
      .keepWildcards = (argc >= 7) ? sqlite_value_bool(argv[6], 0) : 0,
  };

  char *normalized = NULL;
  int normalizedLen = 0;
  int rc = normalize_text_with_options(text, nText, &options, &normalized, &normalizedLen);
  if (rc != SQLITE_OK) {
    sqlite3_free(normalized);
    sqlite3_result_error_nomem(context);
    return;
  }
  sqlite3_result_text(context, normalized, normalizedLen, sqlite3_free);
}

static void udf_sqlite_tokenizer_ar_has_sensitive_forms(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 5) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_has_sensitive_forms expects 5 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_int(context, 0);
    return;
  }
  const char *text = (const char *)sqlite3_value_text(argv[0]);
  int nText = sqlite3_value_bytes(argv[0]);
  if (text == NULL || nText <= 0) {
    sqlite3_result_int(context, 0);
    return;
  }
  int checkDiacritics = sqlite_value_bool(argv[1], 0);
  int checkHamzaForms = sqlite_value_bool(argv[2], 0);
  int checkLetterForms = sqlite_value_bool(argv[3], 0);
  int checkDigitForms = sqlite_value_bool(argv[4], 0);
  int pos = 0;
  while (pos < nText) {
    int consumed = 0;
    uint32_t cp = 0;
    if (!utf8_decode(text + pos, nText - pos, &consumed, &cp)) {
      pos++;
      continue;
    }
    pos += consumed;
    if (checkDiacritics && (cp == 0x0640 || is_arabic_diacritic_strict(cp))) {
      sqlite3_result_int(context, 1);
      return;
    }
    if (checkHamzaForms && (cp == 0x0622 || cp == 0x0623 || cp == 0x0625)) {
      sqlite3_result_int(context, 1);
      return;
    }
    if (checkLetterForms && (cp == 0x0649 || cp == 0x0629)) {
      sqlite3_result_int(context, 1);
      return;
    }
    if (checkDigitForms && is_digit_codepoint(cp)) {
      sqlite3_result_int(context, 1);
      return;
    }
  }
  sqlite3_result_int(context, 0);
}

static int normalize_to_codepoints_with_map(
    const char *input,
    int nInput,
    const NormalizeOptions *options,
    uint32_t **outCodepoints,
    int **outOrigIndexMap,
    int *outCount) {
  int cap = (nInput <= 0) ? 8 : (nInput + 1);
  uint32_t *codepoints = sqlite3_malloc((size_t)cap * sizeof(uint32_t));
  int *origIndexMap = sqlite3_malloc((size_t)cap * sizeof(int));
  if (codepoints == NULL || origIndexMap == NULL) {
    sqlite3_free(codepoints);
    sqlite3_free(origIndexMap);
    return SQLITE_NOMEM;
  }
  int count = 0;
  int inPos = 0;
  int origIndex = 0;
  while (inPos < nInput) {
    int consumed = 0;
    uint32_t cp = 0;
    if (!utf8_decode(input + inPos, nInput - inPos, &consumed, &cp)) {
      inPos++;
      continue;
    }
    inPos += consumed;
    cp = normalize_codepoint_with_options(cp, options);
    if (cp != 0) {
      if (count >= cap) {
        int nextCap = cap * 2;
        uint32_t *nextCodepoints = sqlite3_realloc(codepoints, (size_t)nextCap * sizeof(uint32_t));
        int *nextMap = sqlite3_realloc(origIndexMap, (size_t)nextCap * sizeof(int));
        if (nextCodepoints == NULL || nextMap == NULL) {
          sqlite3_free(nextCodepoints);
          sqlite3_free(nextMap);
          sqlite3_free(codepoints);
          sqlite3_free(origIndexMap);
          return SQLITE_NOMEM;
        }
        codepoints = nextCodepoints;
        origIndexMap = nextMap;
        cap = nextCap;
      }
      codepoints[count] = cp;
      origIndexMap[count] = origIndex;
      count++;
    }
    origIndex++;
  }
  *outCodepoints = codepoints;
  *outOrigIndexMap = origIndexMap;
  *outCount = count;
  return SQLITE_OK;
}

static int find_codepoint_subsequence(
    const uint32_t *haystack,
    int haystackCount,
    const uint32_t *needle,
    int needleCount,
    int startAt) {
  if (needleCount <= 0 || haystackCount <= 0 || startAt < 0 || startAt >= haystackCount) {
    return -1;
  }
  int limit = haystackCount - needleCount;
  for (int pos = startAt; pos <= limit; pos++) {
    int matched = 1;
    for (int i = 0; i < needleCount; i++) {
      if (haystack[pos + i] != needle[i]) {
        matched = 0;
        break;
      }
    }
    if (matched) {
      return pos;
    }
  }
  return -1;
}

static void udf_sqlite_tokenizer_ar_find_all_normalized_match_spans_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc < 2 || argc > 3) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_find_all_normalized_match_spans_json expects 2..3 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }

  const char *text = (const char *)sqlite3_value_text(argv[0]);
  int nText = sqlite3_value_bytes(argv[0]);
  const char *term = (const char *)sqlite3_value_text(argv[1]);
  int nTerm = sqlite3_value_bytes(argv[1]);
  int limit = (argc >= 3) ? sqlite3_value_int(argv[2]) : 8;
  if (text == NULL || term == NULL || nText <= 0 || nTerm <= 0 || limit <= 0) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
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
    sqlite3_result_error_nomem(context);
    return;
  }

  uint32_t *termCodepoints = NULL;
  int *termMapUnused = NULL;
  int termCount = 0;
  rc = normalize_to_codepoints_with_map(term, nTerm, &options, &termCodepoints, &termMapUnused, &termCount);
  sqlite3_free(termMapUnused);
  if (rc != SQLITE_OK) {
    sqlite3_free(textCodepoints);
    sqlite3_free(textOrigIndexMap);
    sqlite3_result_error_nomem(context);
    return;
  }

  if (textCount <= 0 || termCount <= 0 || termCount > textCount) {
    sqlite3_free(textCodepoints);
    sqlite3_free(textOrigIndexMap);
    sqlite3_free(termCodepoints);
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }

  int step = (termCount > 0) ? termCount : 1;
  int cursor = 0;
  sqlite3_str *json = sqlite3_str_new(sqlite3_context_db_handle(context));
  if (json == NULL) {
    sqlite3_free(textCodepoints);
    sqlite3_free(textOrigIndexMap);
    sqlite3_free(termCodepoints);
    sqlite3_result_error_nomem(context);
    return;
  }
  sqlite3_str_appendall(json, "[");
  int emitted = 0;
  while (emitted < limit && cursor < textCount) {
    int pos = find_codepoint_subsequence(textCodepoints, textCount, termCodepoints, termCount, cursor);
    if (pos < 0) {
      break;
    }
    int startOrig = textOrigIndexMap[pos];
    int endOrig = textOrigIndexMap[pos + termCount - 1] + 1;
    if (emitted > 0) {
      sqlite3_str_appendall(json, ",");
    }
    sqlite3_str_appendf(json, "{\"start\":%d,\"end\":%d}", startOrig, endOrig);
    emitted++;
    cursor = pos + step;
  }
  sqlite3_str_appendall(json, "]");
  char *jsonOut = sqlite3_str_finish(json);
  if (jsonOut == NULL) {
    sqlite3_free(textCodepoints);
    sqlite3_free(textOrigIndexMap);
    sqlite3_free(termCodepoints);
    sqlite3_result_error_nomem(context);
    return;
  }

  sqlite3_free(textCodepoints);
  sqlite3_free(textOrigIndexMap);
  sqlite3_free(termCodepoints);
  sqlite3_result_text(context, jsonOut, -1, sqlite3_free);
}

static int decode_text_to_codepoints(const char *input, int nInput, uint32_t **outCodepoints, int *outCount) {
  int cap = (nInput <= 0) ? 8 : (nInput + 1);
  uint32_t *codepoints = sqlite3_malloc((size_t)cap * sizeof(uint32_t));
  if (codepoints == NULL) {
    return SQLITE_NOMEM;
  }
  int count = 0;
  int inPos = 0;
  while (inPos < nInput) {
    int consumed = 0;
    uint32_t cp = 0;
    if (!utf8_decode(input + inPos, nInput - inPos, &consumed, &cp)) {
      inPos++;
      continue;
    }
    inPos += consumed;
    if (count >= cap) {
      int nextCap = cap * 2;
      uint32_t *next = sqlite3_realloc(codepoints, (size_t)nextCap * sizeof(uint32_t));
      if (next == NULL) {
        sqlite3_free(codepoints);
        return SQLITE_NOMEM;
      }
      codepoints = next;
      cap = nextCap;
    }
    codepoints[count++] = cp;
  }
  *outCodepoints = codepoints;
  *outCount = count;
  return SQLITE_OK;
}

static int is_snippet_boundary_codepoint(uint32_t cp) {
  if (cp <= 0x7F && isspace((int)cp)) {
    return 1;
  }
  if (cp <= 0x7F && strchr("`~!@#$%^&*()-_=+[{]}\\|;:'\",<.>/?", (int)cp) != NULL) {
    return 1;
  }
  return cp == 0x060C || cp == 0x061B || cp == 0x00AB || cp == 0x00BB || cp == 0x2026;
}

static void udf_sqlite_tokenizer_ar_snap_snippet_window_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc < 3 || argc > 4) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_snap_snippet_window_json expects 3..4 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_text(context, "{\"start\":0,\"end\":0}", -1, SQLITE_STATIC);
    return;
  }
  const char *text = (const char *)sqlite3_value_text(argv[0]);
  int nText = sqlite3_value_bytes(argv[0]);
  int start = sqlite3_value_int(argv[1]);
  int end = sqlite3_value_int(argv[2]);
  int scan = (argc >= 4) ? sqlite3_value_int(argv[3]) : 12;
  if (text == NULL || nText <= 0) {
    sqlite3_result_text(context, "{\"start\":0,\"end\":0}", -1, SQLITE_STATIC);
    return;
  }

  uint32_t *codepoints = NULL;
  int textLen = 0;
  int rc = decode_text_to_codepoints(text, nText, &codepoints, &textLen);
  if (rc != SQLITE_OK) {
    sqlite3_result_error_nomem(context);
    return;
  }

  if (start < 0) {
    start = 0;
  }
  if (end < 0) {
    end = 0;
  }
  if (start > textLen) {
    start = textLen;
  }
  if (end > textLen) {
    end = textLen;
  }
  if (scan < 0) {
    scan = 0;
  }

  int outStart = start;
  int leftBound = start - scan;
  if (leftBound < 0) {
    leftBound = 0;
  }
  int i = start;
  while (i > leftBound) {
    if (is_snippet_boundary_codepoint(codepoints[i - 1])) {
      outStart = i;
      break;
    }
    i--;
  }

  int outEnd = end;
  int rightBound = end + scan;
  if (rightBound > textLen) {
    rightBound = textLen;
  }
  i = end;
  while (i < rightBound) {
    if (is_snippet_boundary_codepoint(codepoints[i])) {
      outEnd = i;
      break;
    }
    i++;
  }

  if (outEnd <= outStart) {
    outStart = start;
    outEnd = end;
    if (outEnd < outStart) {
      outEnd = outStart;
    }
  }

  sqlite3_free(codepoints);
  sqlite3_result_text(context, sqlite3_mprintf("{\"start\":%d,\"end\":%d}", outStart, outEnd), -1, sqlite3_free);
}

typedef struct SnippetRankedSpan {
  int start;
  int end;
  int priority;
} SnippetRankedSpan;

typedef struct SnippetSpan {
  int start;
  int end;
} SnippetSpan;

static int compare_ranked_spans(const void *left, const void *right) {
  const SnippetRankedSpan *a = (const SnippetRankedSpan *)left;
  const SnippetRankedSpan *b = (const SnippetRankedSpan *)right;
  if (a->priority != b->priority) {
    return (a->priority < b->priority) ? -1 : 1;
  }
  int lenA = a->end - a->start;
  int lenB = b->end - b->start;
  if (lenA != lenB) {
    return (lenA > lenB) ? -1 : 1;
  }
  if (a->start != b->start) {
    return (a->start < b->start) ? -1 : 1;
  }
  if (a->end != b->end) {
    return (a->end < b->end) ? -1 : 1;
  }
  return 0;
}

static int compare_spans_asc(const void *left, const void *right) {
  const SnippetSpan *a = (const SnippetSpan *)left;
  const SnippetSpan *b = (const SnippetSpan *)right;
  if (a->start != b->start) {
    return (a->start < b->start) ? -1 : 1;
  }
  if (a->end != b->end) {
    return (a->end < b->end) ? -1 : 1;
  }
  return 0;
}

static int spans_overlap_inclusive(int aStart, int aEnd, int bStart, int bEnd) {
  return !(aEnd <= bStart || aStart >= bEnd);
}

static int parse_ranked_span_token(const char *token, SnippetRankedSpan *outSpan) {
  if (token == NULL || token[0] == '\0') {
    return 0;
  }
  char *endPtr = NULL;
  long start = strtol(token, &endPtr, 10);
  if (endPtr == token || endPtr == NULL || *endPtr != ':') {
    return 0;
  }
  const char *afterStart = endPtr + 1;
  long end = strtol(afterStart, &endPtr, 10);
  if (endPtr == afterStart || endPtr == NULL || *endPtr != ':') {
    return 0;
  }
  const char *afterEnd = endPtr + 1;
  long priority = strtol(afterEnd, &endPtr, 10);
  if (endPtr == afterEnd || endPtr == NULL || *endPtr != '\0') {
    return 0;
  }
  if (start < 0 || end <= start) {
    return 0;
  }
  outSpan->start = (int)start;
  outSpan->end = (int)end;
  outSpan->priority = (int)priority;
  return 1;
}

static void udf_sqlite_tokenizer_ar_select_non_overlapping_spans_csv(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_select_non_overlapping_spans_csv expects 1 argument", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_text(context, "", 0, SQLITE_STATIC);
    return;
  }

  const char *csv = (const char *)sqlite3_value_text(argv[0]);
  int nCsv = sqlite3_value_bytes(argv[0]);
  if (csv == NULL || nCsv <= 0) {
    sqlite3_result_text(context, "", 0, SQLITE_STATIC);
    return;
  }

  char *copy = sqlite3_malloc((size_t)nCsv + 1);
  if (copy == NULL) {
    sqlite3_result_error_nomem(context);
    return;
  }
  memcpy(copy, csv, (size_t)nCsv);
  copy[nCsv] = '\0';

  int cap = 8;
  int count = 0;
  SnippetRankedSpan *ranked = sqlite3_malloc((size_t)cap * sizeof(SnippetRankedSpan));
  if (ranked == NULL) {
    sqlite3_free(copy);
    sqlite3_result_error_nomem(context);
    return;
  }

  char *cursor = copy;
  while (cursor != NULL && *cursor != '\0') {
    char *sep = strchr(cursor, ';');
    if (sep != NULL) {
      *sep = '\0';
    }

    SnippetRankedSpan span;
    if (parse_ranked_span_token(cursor, &span)) {
      if (count >= cap) {
        int nextCap = cap * 2;
        SnippetRankedSpan *next = sqlite3_realloc(ranked, (size_t)nextCap * sizeof(SnippetRankedSpan));
        if (next == NULL) {
          sqlite3_free(copy);
          sqlite3_free(ranked);
          sqlite3_result_error_nomem(context);
          return;
        }
        ranked = next;
        cap = nextCap;
      }
      ranked[count++] = span;
    }

    cursor = (sep == NULL) ? NULL : (sep + 1);
  }

  sqlite3_free(copy);
  if (count == 0) {
    sqlite3_free(ranked);
    sqlite3_result_text(context, "", 0, SQLITE_STATIC);
    return;
  }

  qsort(ranked, (size_t)count, sizeof(SnippetRankedSpan), compare_ranked_spans);

  SnippetSpan *picked = sqlite3_malloc((size_t)count * sizeof(SnippetSpan));
  if (picked == NULL) {
    sqlite3_free(ranked);
    sqlite3_result_error_nomem(context);
    return;
  }
  int pickedCount = 0;
  for (int i = 0; i < count; i++) {
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

  qsort(picked, (size_t)pickedCount, sizeof(SnippetSpan), compare_spans_asc);

  sqlite3_str *out = sqlite3_str_new(sqlite3_context_db_handle(context));
  if (out == NULL) {
    sqlite3_free(ranked);
    sqlite3_free(picked);
    sqlite3_result_error_nomem(context);
    return;
  }
  for (int i = 0; i < pickedCount; i++) {
    if (i > 0) {
      sqlite3_str_appendall(out, ";");
    }
    sqlite3_str_appendf(out, "%d:%d", picked[i].start, picked[i].end);
  }
  char *outText = sqlite3_str_finish(out);
  if (outText == NULL) {
    sqlite3_free(ranked);
    sqlite3_free(picked);
    sqlite3_result_error_nomem(context);
    return;
  }

  sqlite3_free(ranked);
  sqlite3_free(picked);
  sqlite3_result_text(context, outText, -1, sqlite3_free);
}

static int parse_span_token(const char *token, SnippetSpan *outSpan) {
  if (token == NULL || token[0] == '\0') {
    return 0;
  }
  char *endPtr = NULL;
  long start = strtol(token, &endPtr, 10);
  if (endPtr == token || endPtr == NULL || *endPtr != ':') {
    return 0;
  }
  const char *afterStart = endPtr + 1;
  long end = strtol(afterStart, &endPtr, 10);
  if (endPtr == afterStart || endPtr == NULL || *endPtr != '\0') {
    return 0;
  }
  if (start < 0 || end <= start) {
    return 0;
  }
  outSpan->start = (int)start;
  outSpan->end = (int)end;
  return 1;
}

static int compare_spans_desc(const void *left, const void *right) {
  const SnippetSpan *a = (const SnippetSpan *)left;
  const SnippetSpan *b = (const SnippetSpan *)right;
  if (a->start != b->start) {
    return (a->start > b->start) ? -1 : 1;
  }
  if (a->end != b->end) {
    return (a->end > b->end) ? -1 : 1;
  }
  return 0;
}

static int ensure_codepoint_capacity(uint32_t **buffer, int *capacity, int required) {
  if (required <= *capacity) {
    return SQLITE_OK;
  }
  int nextCap = *capacity;
  while (nextCap < required) {
    nextCap *= 2;
    if (nextCap < 0) {
      return SQLITE_NOMEM;
    }
  }
  uint32_t *next = sqlite3_realloc(*buffer, (size_t)nextCap * sizeof(uint32_t));
  if (next == NULL) {
    return SQLITE_NOMEM;
  }
  *buffer = next;
  *capacity = nextCap;
  return SQLITE_OK;
}

static int encode_codepoints_to_utf8_text(const uint32_t *codepoints, int count, char **outText, int *outLen) {
  int cap = (count <= 0) ? 4 : (count * 4 + 4);
  char *out = sqlite3_malloc((size_t)cap);
  if (out == NULL) {
    return SQLITE_NOMEM;
  }
  int outPos = 0;
  for (int i = 0; i < count; i++) {
    char encoded[4];
    int encodedLen = utf8_encode(codepoints[i], encoded);
    if (outPos + encodedLen + 1 > cap) {
      int nextCap = cap * 2;
      if (nextCap < outPos + encodedLen + 1) {
        nextCap = outPos + encodedLen + 1;
      }
      char *next = sqlite3_realloc(out, (size_t)nextCap);
      if (next == NULL) {
        sqlite3_free(out);
        return SQLITE_NOMEM;
      }
      out = next;
      cap = nextCap;
    }
    memcpy(out + outPos, encoded, (size_t)encodedLen);
    outPos += encodedLen;
  }
  out[outPos] = '\0';
  *outText = out;
  *outLen = outPos;
  return SQLITE_OK;
}

static void udf_sqlite_tokenizer_ar_render_snippet_with_highlights(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 4) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_render_snippet_with_highlights expects 4 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_text(context, "", 0, SQLITE_STATIC);
    return;
  }

  const char *text = (const char *)sqlite3_value_text(argv[0]);
  int nText = sqlite3_value_bytes(argv[0]);
  int start = sqlite3_value_int(argv[1]);
  int end = sqlite3_value_int(argv[2]);
  const char *spansCsv = (const char *)sqlite3_value_text(argv[3]);
  int nSpansCsv = sqlite3_value_bytes(argv[3]);
  if (text == NULL || nText <= 0) {
    sqlite3_result_text(context, "", 0, SQLITE_STATIC);
    return;
  }

  uint32_t *textCodepoints = NULL;
  int textLen = 0;
  int rc = decode_text_to_codepoints(text, nText, &textCodepoints, &textLen);
  if (rc != SQLITE_OK) {
    sqlite3_result_error_nomem(context);
    return;
  }

  if (start < 0) {
    start = 0;
  }
  if (end < 0) {
    end = 0;
  }
  if (start > textLen) {
    start = textLen;
  }
  if (end > textLen) {
    end = textLen;
  }
  if (end < start) {
    end = start;
  }

  int snippetLen = end - start;
  int snippetCap = snippetLen + 16;
  if (snippetCap < 16) {
    snippetCap = 16;
  }
  uint32_t *snippet = sqlite3_malloc((size_t)snippetCap * sizeof(uint32_t));
  if (snippet == NULL) {
    sqlite3_free(textCodepoints);
    sqlite3_result_error_nomem(context);
    return;
  }
  if (snippetLen > 0) {
    memcpy(snippet, textCodepoints + start, (size_t)snippetLen * sizeof(uint32_t));
  }

  int spanCount = 0;
  int spanCap = 8;
  SnippetSpan *spans = sqlite3_malloc((size_t)spanCap * sizeof(SnippetSpan));
  if (spans == NULL) {
    sqlite3_free(textCodepoints);
    sqlite3_free(snippet);
    sqlite3_result_error_nomem(context);
    return;
  }
  if (spansCsv != NULL && nSpansCsv > 0) {
    char *copy = sqlite3_malloc((size_t)nSpansCsv + 1);
    if (copy == NULL) {
      sqlite3_free(textCodepoints);
      sqlite3_free(snippet);
      sqlite3_free(spans);
      sqlite3_result_error_nomem(context);
      return;
    }
    memcpy(copy, spansCsv, (size_t)nSpansCsv);
    copy[nSpansCsv] = '\0';
    char *cursor = copy;
    while (cursor != NULL && *cursor != '\0') {
      char *sep = strchr(cursor, ';');
      if (sep != NULL) {
        *sep = '\0';
      }
      SnippetSpan span;
      if (parse_span_token(cursor, &span)) {
        if (spanCount >= spanCap) {
          int nextCap = spanCap * 2;
          SnippetSpan *next = sqlite3_realloc(spans, (size_t)nextCap * sizeof(SnippetSpan));
          if (next == NULL) {
            sqlite3_free(copy);
            sqlite3_free(textCodepoints);
            sqlite3_free(snippet);
            sqlite3_free(spans);
            sqlite3_result_error_nomem(context);
            return;
          }
          spans = next;
          spanCap = nextCap;
        }
        spans[spanCount++] = span;
      }
      cursor = (sep == NULL) ? NULL : (sep + 1);
    }
    sqlite3_free(copy);
  }

  if (spanCount > 0) {
    qsort(spans, (size_t)spanCount, sizeof(SnippetSpan), compare_spans_desc);
  }
  for (int i = 0; i < spanCount; i++) {
    int relStart = spans[i].start - start;
    if (relStart < 0) {
      relStart = 0;
    }
    if (relStart > snippetLen) {
      relStart = snippetLen;
    }
    int relEnd = spans[i].end - start;
    if (relEnd < relStart) {
      relEnd = relStart;
    }
    if (relEnd > snippetLen) {
      relEnd = snippetLen;
    }

    rc = ensure_codepoint_capacity(&snippet, &snippetCap, snippetLen + 4 + 2);
    if (rc != SQLITE_OK) {
      sqlite3_free(textCodepoints);
      sqlite3_free(snippet);
      sqlite3_free(spans);
      sqlite3_result_error_nomem(context);
      return;
    }

    memmove(
        snippet + relEnd + 2,
        snippet + relEnd,
        (size_t)(snippetLen - relEnd) * sizeof(uint32_t));
    snippet[relEnd] = ']';
    snippet[relEnd + 1] = ']';
    snippetLen += 2;

    memmove(
        snippet + relStart + 2,
        snippet + relStart,
        (size_t)(snippetLen - relStart) * sizeof(uint32_t));
    snippet[relStart] = '[';
    snippet[relStart + 1] = '[';
    snippetLen += 2;
  }

  if (start > 0) {
    rc = ensure_codepoint_capacity(&snippet, &snippetCap, snippetLen + 2);
    if (rc != SQLITE_OK) {
      sqlite3_free(textCodepoints);
      sqlite3_free(snippet);
      sqlite3_free(spans);
      sqlite3_result_error_nomem(context);
      return;
    }
    memmove(snippet + 1, snippet, (size_t)snippetLen * sizeof(uint32_t));
    snippet[0] = 0x2026;
    snippetLen++;
  }
  if (end < textLen) {
    rc = ensure_codepoint_capacity(&snippet, &snippetCap, snippetLen + 2);
    if (rc != SQLITE_OK) {
      sqlite3_free(textCodepoints);
      sqlite3_free(snippet);
      sqlite3_free(spans);
      sqlite3_result_error_nomem(context);
      return;
    }
    snippet[snippetLen++] = 0x2026;
  }

  char *outText = NULL;
  int outLen = 0;
  rc = encode_codepoints_to_utf8_text(snippet, snippetLen, &outText, &outLen);
  sqlite3_free(textCodepoints);
  sqlite3_free(snippet);
  sqlite3_free(spans);
  if (rc != SQLITE_OK) {
    sqlite3_result_error_nomem(context);
    return;
  }
  sqlite3_result_text(context, outText, outLen, sqlite3_free);
}

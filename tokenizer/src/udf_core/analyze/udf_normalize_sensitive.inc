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
  int cap = 8;
  if (nInput > 0 && ar_checked_int_sum(nInput, 1, &cap) != SQLITE_OK) {
    return SQLITE_TOOBIG;
  }
  uint32_t *codepoints = ar_malloc_array(cap, sizeof(uint32_t));
  int *origIndexMap = ar_malloc_array(cap, sizeof(int));
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
        int nextCap = 0;
        int growRc = ar_grow_capacity(cap, count + 1, 8, &nextCap);
        if (growRc != SQLITE_OK) {
          sqlite3_free(codepoints);
          sqlite3_free(origIndexMap);
          return growRc;
        }
        uint32_t *nextCodepoints = ar_realloc_array(codepoints, nextCap, sizeof(uint32_t));
        if (nextCodepoints == NULL) {
          sqlite3_free(codepoints);
          sqlite3_free(origIndexMap);
          return SQLITE_NOMEM;
        }
        codepoints = nextCodepoints;
        int *nextMap = ar_realloc_array(origIndexMap, nextCap, sizeof(int));
        if (nextMap == NULL) {
          sqlite3_free(codepoints);
          sqlite3_free(origIndexMap);
          return SQLITE_NOMEM;
        }
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
  int cap = 8;
  if (nInput > 0 && ar_checked_int_sum(nInput, 1, &cap) != SQLITE_OK) {
    return SQLITE_TOOBIG;
  }
  uint32_t *codepoints = ar_malloc_array(cap, sizeof(uint32_t));
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
      int nextCap = 0;
      int growRc = ar_grow_capacity(cap, count + 1, 8, &nextCap);
      if (growRc != SQLITE_OK) {
        sqlite3_free(codepoints);
        return growRc;
      }
      uint32_t *next = ar_realloc_array(codepoints, nextCap, sizeof(uint32_t));
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

static int select_highlight_spans(SnippetSpan *spans, int spanCount, SnippetSpan **outSpans, int *outCount);

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


static int parse_int_until_char(const char *text, char terminator, const char **after, int *outValue) {
  if (text == NULL || text[0] == '\0') {
    return 0;
  }
  errno = 0;
  char *endPtr = NULL;
  long value = strtol(text, &endPtr, 10);
  if (endPtr == text || endPtr == NULL || errno == ERANGE || value < INT_MIN || value > INT_MAX) {
    return 0;
  }
  if ((terminator == '\0' && *endPtr != '\0') || (terminator != '\0' && *endPtr != terminator)) {
    return 0;
  }
  *outValue = (int)value;
  *after = endPtr + (terminator == '\0' ? 0 : 1);
  return 1;
}

static int parse_ranked_span_token(const char *token, SnippetRankedSpan *outSpan) {
  if (token == NULL || token[0] == '\0') {
    return 0;
  }
  const char *afterStart = NULL;
  const char *afterEnd = NULL;
  const char *afterPriority = NULL;
  int start = 0;
  int end = 0;
  int priority = 0;
  if (!parse_int_until_char(token, ':', &afterStart, &start) || !parse_int_until_char(afterStart, ':', &afterEnd, &end) ||
      !parse_int_until_char(afterEnd, '\0', &afterPriority, &priority)) {
    return 0;
  }
  (void)afterPriority;
  if (start < 0 || end <= start) {
    return 0;
  }
  outSpan->start = start;
  outSpan->end = end;
  outSpan->priority = priority;
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

  int copyCap = 0;
  if (ar_checked_int_sum(nCsv, 1, &copyCap) != SQLITE_OK) {
    sqlite3_result_error_toobig(context);
    return;
  }
  char *copy = sqlite3_malloc64((sqlite3_uint64)copyCap);
  if (copy == NULL) {
    sqlite3_result_error_nomem(context);
    return;
  }
  memcpy(copy, csv, (size_t)nCsv);
  copy[nCsv] = '\0';

  int cap = 8;
  int count = 0;
  SnippetRankedSpan *ranked = ar_malloc_array(cap, sizeof(SnippetRankedSpan));
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
        int nextCap = 0;
        int growRc = ar_grow_capacity(cap, count + 1, 8, &nextCap);
        if (growRc != SQLITE_OK) {
          sqlite3_free(copy);
          sqlite3_free(ranked);
          sqlite3_result_error_toobig(context);
          return;
        }
        SnippetRankedSpan *next = ar_realloc_array(ranked, nextCap, sizeof(SnippetRankedSpan));
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

  SnippetSpan *picked = ar_malloc_array(count, sizeof(SnippetSpan));
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
  const char *afterStart = NULL;
  const char *afterEnd = NULL;
  int start = 0;
  int end = 0;
  if (!parse_int_until_char(token, ':', &afterStart, &start) || !parse_int_until_char(afterStart, '\0', &afterEnd, &end)) {
    return 0;
  }
  (void)afterEnd;
  if (start < 0 || end <= start) {
    return 0;
  }
  outSpan->start = start;
  outSpan->end = end;
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
  int nextCap = 0;
  int rc = ar_grow_capacity(*capacity, required, 8, &nextCap);
  if (rc != SQLITE_OK) {
    return rc;
  }
  uint32_t *next = ar_realloc_array(*buffer, nextCap, sizeof(uint32_t));
  if (next == NULL) {
    return SQLITE_NOMEM;
  }
  *buffer = next;
  *capacity = nextCap;
  return SQLITE_OK;
}

static int encode_codepoints_to_utf8_text(const uint32_t *codepoints, int count, char **outText, int *outLen) {
  int cap = 0;
  int capRc = ar_utf8_expanded_capacity(count, &cap);
  if (capRc != SQLITE_OK) {
    return capRc;
  }
  char *out = sqlite3_malloc64((sqlite3_uint64)cap);
  if (out == NULL) {
    return SQLITE_NOMEM;
  }
  int outPos = 0;
  for (int i = 0; i < count; i++) {
    char encoded[4];
    int encodedLen = utf8_encode(codepoints[i], encoded);
    if (encodedLen <= 0) {
      continue;
    }
    int required = 0;
    if (ar_checked_int_sum(outPos, encodedLen + 1, &required) != SQLITE_OK) {
      sqlite3_free(out);
      return SQLITE_TOOBIG;
    }
    if (required > cap) {
      int nextCap = 0;
      int growRc = ar_grow_capacity(cap, required, 64, &nextCap);
      if (growRc != SQLITE_OK) {
        sqlite3_free(out);
        return growRc;
      }
      char *next = sqlite3_realloc64(out, (sqlite3_uint64)nextCap);
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

typedef struct JsonStringArray {
  char **items;
  int *lens;
  int count;
  int cap;
} JsonStringArray;

static void json_string_array_free(JsonStringArray *array) {
  if (array == NULL) {
    return;
  }
  for (int i = 0; i < array->count; i++) {
    sqlite3_free(array->items[i]);
  }
  sqlite3_free(array->items);
  sqlite3_free(array->lens);
  memset(array, 0, sizeof(*array));
}

static int json_skip_ws(const char *json, int nJson, int pos) {
  while (pos < nJson && isspace((unsigned char)json[pos])) {
    pos++;
  }
  return pos;
}

static int json_hex_digit_value(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}


static int json_parse_hex4_at(const char *json, int nJson, int pos, uint32_t *outCodepoint) {
  if (pos + 4 > nJson) {
    return SQLITE_ERROR;
  }
  uint32_t cp = 0;
  for (int i = 0; i < 4; i++) {
    int digit = json_hex_digit_value(json[pos + i]);
    if (digit < 0) {
      return SQLITE_ERROR;
    }
    cp = (cp << 4) | (uint32_t)digit;
  }
  *outCodepoint = cp;
  return SQLITE_OK;
}

static int json_string_value_ensure(char **value, int *cap, int required) {
  if (required <= *cap) {
    return SQLITE_OK;
  }
  int nextCap = 0;
  int rc = ar_grow_capacity(*cap, required, 32, &nextCap);
  if (rc != SQLITE_OK) {
    return rc;
  }
  char *next = sqlite3_realloc64(*value, (sqlite3_uint64)nextCap);
  if (next == NULL) {
    return SQLITE_NOMEM;
  }
  *value = next;
  *cap = nextCap;
  return SQLITE_OK;
}

static int json_string_value_append_byte(char **value, int *outPos, int *cap, unsigned char c) {
  int required = 0;
  if (ar_checked_int_sum(*outPos, 2, &required) != SQLITE_OK) {
    return SQLITE_TOOBIG;
  }
  int rc = json_string_value_ensure(value, cap, required);
  if (rc != SQLITE_OK) {
    return rc;
  }
  (*value)[(*outPos)++] = (char)c;
  return SQLITE_OK;
}

static int json_string_value_append_codepoint(char **value, int *outPos, int *cap, uint32_t cp) {
  char encoded[4];
  int encodedLen = utf8_encode(cp, encoded);
  if (encodedLen <= 0) {
    return SQLITE_ERROR;
  }
  int required = 0;
  if (ar_checked_int_sum(*outPos, encodedLen + 1, &required) != SQLITE_OK) {
    return SQLITE_TOOBIG;
  }
  int rc = json_string_value_ensure(value, cap, required);
  if (rc != SQLITE_OK) {
    return rc;
  }
  memcpy(*value + *outPos, encoded, (size_t)encodedLen);
  *outPos += encodedLen;
  return SQLITE_OK;
}

static int json_string_array_append(JsonStringArray *array, char *value, int nValue) {
  if (array->count >= array->cap) {
    int required = 0;
    if (ar_checked_int_sum(array->count, 1, &required) != SQLITE_OK) {
      return SQLITE_TOOBIG;
    }
    int nextCap = 0;
    int rc = ar_grow_capacity(array->cap, required, 4, &nextCap);
    if (rc != SQLITE_OK) {
      return rc;
    }
    char **nextItems = ar_realloc_array(array->items, nextCap, sizeof(char *));
    if (nextItems == NULL) {
      return SQLITE_NOMEM;
    }
    array->items = nextItems;
    int *nextLens = ar_realloc_array(array->lens, nextCap, sizeof(int));
    if (nextLens == NULL) {
      return SQLITE_NOMEM;
    }
    array->lens = nextLens;
    array->cap = nextCap;
  }
  array->items[array->count] = value;
  array->lens[array->count] = nValue;
  array->count++;
  return SQLITE_OK;
}

static int parse_json_string_value(const char *json, int nJson, int *pos, char **out, int *outLen) {
  if (*pos >= nJson || json[*pos] != '"') {
    return SQLITE_ERROR;
  }
  int cap = 32;
  char *value = sqlite3_malloc64((sqlite3_uint64)cap);
  if (value == NULL) {
    return SQLITE_NOMEM;
  }
  int outPos = 0;
  (*pos)++;
  while (*pos < nJson) {
    unsigned char c = (unsigned char)json[*pos];
    (*pos)++;
    if (c == '"') {
      value[outPos] = '\0';
      *out = value;
      *outLen = outPos;
      return SQLITE_OK;
    }
    if (c < 0x20) {
      sqlite3_free(value);
      return SQLITE_ERROR;
    }
    if (c != '\\') {
      int rc = json_string_value_append_byte(&value, &outPos, &cap, c);
      if (rc != SQLITE_OK) {
        sqlite3_free(value);
        return rc;
      }
      continue;
    }

    if (*pos >= nJson) {
      sqlite3_free(value);
      return SQLITE_ERROR;
    }
    c = (unsigned char)json[*pos];
    (*pos)++;
    if (c == '"' || c == '\\' || c == '/') {
      int rc = json_string_value_append_byte(&value, &outPos, &cap, c);
      if (rc != SQLITE_OK) {
        sqlite3_free(value);
        return rc;
      }
      continue;
    }
    if (c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't') {
      unsigned char escaped = '\0';
      if (c == 'b') {
        escaped = '\b';
      } else if (c == 'f') {
        escaped = '\f';
      } else if (c == 'n') {
        escaped = '\n';
      } else if (c == 'r') {
        escaped = '\r';
      } else {
        escaped = '\t';
      }
      int rc = json_string_value_append_byte(&value, &outPos, &cap, escaped);
      if (rc != SQLITE_OK) {
        sqlite3_free(value);
        return rc;
      }
      continue;
    }
    if (c == 'u') {
      uint32_t cp = 0;
      int rc = json_parse_hex4_at(json, nJson, *pos, &cp);
      if (rc != SQLITE_OK) {
        sqlite3_free(value);
        return rc;
      }
      *pos += 4;
      if (cp >= 0xD800 && cp <= 0xDBFF) {
        if (*pos + 6 > nJson || json[*pos] != '\\' || json[*pos + 1] != 'u') {
          sqlite3_free(value);
          return SQLITE_ERROR;
        }
        uint32_t low = 0;
        rc = json_parse_hex4_at(json, nJson, *pos + 2, &low);
        if (rc != SQLITE_OK || low < 0xDC00 || low > 0xDFFF) {
          sqlite3_free(value);
          return SQLITE_ERROR;
        }
        *pos += 6;
        cp = 0x10000 + (((cp - 0xD800) << 10) | (low - 0xDC00));
      } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
        sqlite3_free(value);
        return SQLITE_ERROR;
      }
      rc = json_string_value_append_codepoint(&value, &outPos, &cap, cp);
      if (rc != SQLITE_OK) {
        sqlite3_free(value);
        return rc;
      }
      continue;
    }
    sqlite3_free(value);
    return SQLITE_ERROR;
  }
  sqlite3_free(value);
  return SQLITE_ERROR;
}

static int parse_json_string_array(const char *json, int nJson, JsonStringArray *outArray) {
  memset(outArray, 0, sizeof(*outArray));
  int pos = json_skip_ws(json, nJson, 0);
  if (pos >= nJson || json[pos] != '[') {
    return SQLITE_ERROR;
  }
  pos++;
  pos = json_skip_ws(json, nJson, pos);
  if (pos < nJson && json[pos] == ']') {
    pos++;
    pos = json_skip_ws(json, nJson, pos);
    return pos == nJson ? SQLITE_OK : SQLITE_ERROR;
  }
  while (pos < nJson) {
    char *value = NULL;
    int nValue = 0;
    int rc = parse_json_string_value(json, nJson, &pos, &value, &nValue);
    if (rc != SQLITE_OK) {
      json_string_array_free(outArray);
      return rc;
    }
    rc = json_string_array_append(outArray, value, nValue);
    if (rc != SQLITE_OK) {
      sqlite3_free(value);
      json_string_array_free(outArray);
      return rc;
    }
    pos = json_skip_ws(json, nJson, pos);
    if (pos < nJson && json[pos] == ',') {
      pos++;
      pos = json_skip_ws(json, nJson, pos);
      continue;
    }
    if (pos < nJson && json[pos] == ']') {
      pos++;
      pos = json_skip_ws(json, nJson, pos);
      return pos == nJson ? SQLITE_OK : SQLITE_ERROR;
    }
    json_string_array_free(outArray);
    return SQLITE_ERROR;
  }
  json_string_array_free(outArray);
  return SQLITE_ERROR;
}

static int append_snippet_span(SnippetSpan **spans, int *count, int *cap, int start, int end) {
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
    rc = ar_grow_capacity(*cap, required, 8, &nextCap);
    if (rc != SQLITE_OK) {
      return rc;
    }
    SnippetSpan *next = ar_realloc_array(*spans, nextCap, sizeof(SnippetSpan));
    if (next == NULL) {
      return SQLITE_NOMEM;
    }
    *spans = next;
    *cap = nextCap;
  }
  (*spans)[*count].start = start;
  (*spans)[*count].end = end;
  (*count)++;
  return SQLITE_OK;
}

typedef struct SourceAnalyzedToken {
  char *term;
  int nTerm;
  int start;
  int end;
  int order;
} SourceAnalyzedToken;

typedef struct SourceAnalyzedTokenBuffer {
  SourceAnalyzedToken *items;
  int count;
  int cap;
  int order;
  const char *text;
  int nText;
  int rc;
} SourceAnalyzedTokenBuffer;

static int byte_offset_to_codepoint_index(const char *text, int nText, int byteOffset) {
  if (byteOffset <= 0 || text == NULL || nText <= 0) {
    return 0;
  }
  if (byteOffset > nText) {
    byteOffset = nText;
  }
  int bytePos = 0;
  int cpIndex = 0;
  while (bytePos < byteOffset) {
    int consumed = 0;
    uint32_t cp = 0;
    if (!utf8_decode(text + bytePos, nText - bytePos, &consumed, &cp) || consumed <= 0) {
      consumed = 1;
    }
    if (bytePos + consumed > byteOffset) {
      break;
    }
    bytePos += consumed;
    cpIndex++;
  }
  return cpIndex;
}

static void source_analyzed_token_buffer_free(SourceAnalyzedTokenBuffer *buffer) {
  if (buffer == NULL) {
    return;
  }
  for (int i = 0; i < buffer->count; i++) {
    sqlite3_free(buffer->items[i].term);
  }
  sqlite3_free(buffer->items);
  memset(buffer, 0, sizeof(*buffer));
}

static int source_analyzed_token_buffer_append(
    SourceAnalyzedTokenBuffer *buffer,
    const char *term,
    int nTerm,
    int start,
    int end) {
  if (buffer->rc != SQLITE_OK) {
    return buffer->rc;
  }
  if (nTerm <= 0 || end <= start) {
    return SQLITE_OK;
  }
  if (buffer->count >= buffer->cap) {
    int required = 0;
    if (ar_checked_int_sum(buffer->count, 1, &required) != SQLITE_OK) {
      buffer->rc = SQLITE_TOOBIG;
      return SQLITE_TOOBIG;
    }
    int nextCap = 0;
    int rc = ar_grow_capacity(buffer->cap, required, 16, &nextCap);
    if (rc != SQLITE_OK) {
      buffer->rc = rc;
      return rc;
    }
    SourceAnalyzedToken *next = ar_realloc_array(buffer->items, nextCap, sizeof(SourceAnalyzedToken));
    if (next == NULL) {
      buffer->rc = SQLITE_NOMEM;
      return SQLITE_NOMEM;
    }
    buffer->items = next;
    buffer->cap = nextCap;
  }
  char *copy = sqlite3_malloc64((sqlite3_uint64)nTerm + 1);
  if (copy == NULL) {
    buffer->rc = SQLITE_NOMEM;
    return SQLITE_NOMEM;
  }
  memcpy(copy, term, (size_t)nTerm);
  copy[nTerm] = '\0';
  buffer->items[buffer->count].term = copy;
  buffer->items[buffer->count].nTerm = nTerm;
  buffer->items[buffer->count].start = start;
  buffer->items[buffer->count].end = end;
  buffer->items[buffer->count].order = buffer->order++;
  buffer->count++;
  return SQLITE_OK;
}

static int source_analyzed_token_collect_callback(void *pCtx, int tflags, const char *pToken, int nToken, int iStart, int iEnd) {
  (void)tflags;
  SourceAnalyzedTokenBuffer *buffer = (SourceAnalyzedTokenBuffer *)pCtx;
  if (nToken == EMPTY_TERM_SENTINEL_LEN && memcmp(pToken, EMPTY_TERM_SENTINEL, (size_t)EMPTY_TERM_SENTINEL_LEN) == 0) {
    return SQLITE_OK;
  }
  int start = byte_offset_to_codepoint_index(buffer->text, buffer->nText, iStart);
  int end = byte_offset_to_codepoint_index(buffer->text, buffer->nText, iEnd);
  return source_analyzed_token_buffer_append(buffer, pToken, nToken, start, end);
}

static int compare_source_analyzed_tokens(const void *left, const void *right) {
  const SourceAnalyzedToken *a = (const SourceAnalyzedToken *)left;
  const SourceAnalyzedToken *b = (const SourceAnalyzedToken *)right;
  if (a->start != b->start) {
    return (a->start < b->start) ? -1 : 1;
  }
  if (a->order != b->order) {
    return (a->order < b->order) ? -1 : 1;
  }
  return 0;
}

static int collect_source_analyzed_tokens(const char *text, int nText, SourceAnalyzedTokenBuffer *tokens) {
  memset(tokens, 0, sizeof(*tokens));
  tokens->text = text;
  tokens->nText = nText;
  ArabicTokenizer tokenizer;
  memset(&tokenizer, 0, sizeof(tokenizer));
  int rc = arabic_tokenize((Fts5Tokenizer *)&tokenizer, tokens, FTS5_TOKENIZE_QUERY, text, nText, source_analyzed_token_collect_callback);
  if (rc == SQLITE_OK) {
    rc = tokens->rc;
  }
  if (rc != SQLITE_OK) {
    source_analyzed_token_buffer_free(tokens);
    return rc;
  }

  int bytePos = 0;
  int cpPos = 0;
  while (bytePos < nText) {
    int consumed = 0;
    uint32_t cp = 0;
    if (!utf8_decode(text + bytePos, nText - bytePos, &consumed, &cp) || consumed <= 0) {
      consumed = 1;
    }
    const HonorificExpansion *expansion = find_honorific_expansion(text + bytePos, nText - bytePos);
    if (expansion != NULL) {
      AnalyzeTermsBuffer expandedTerms;
      memset(&expandedTerms, 0, sizeof(expandedTerms));
      rc = arabic_tokenize(
          (Fts5Tokenizer *)&tokenizer,
          &expandedTerms,
          FTS5_TOKENIZE_QUERY,
          expansion->phrase,
          expansion->phraseLen,
          analyze_collect_token);
      if (rc == SQLITE_OK) {
        rc = expandedTerms.rc;
      }
      for (int i = 0; rc == SQLITE_OK && i < expandedTerms.count; i++) {
        rc = source_analyzed_token_buffer_append(tokens, expandedTerms.terms[i], expandedTerms.termLens[i], cpPos, cpPos + 1);
      }
      analyze_terms_buffer_free(&expandedTerms);
      if (rc != SQLITE_OK) {
        source_analyzed_token_buffer_free(tokens);
        return rc;
      }
      bytePos += expansion->glyphLen;
      cpPos++;
      continue;
    }
    bytePos += consumed;
    cpPos++;
  }

  if (tokens->count > 1) {
    qsort(tokens->items, (size_t)tokens->count, sizeof(SourceAnalyzedToken), compare_source_analyzed_tokens);
  }
  return SQLITE_OK;
}

static int source_token_matches_term(const SourceAnalyzedToken *token, const char *term, int nTerm) {
  return token->nTerm == nTerm && memcmp(token->term, term, (size_t)nTerm) == 0;
}

static int collect_analyzed_match_spans(
    const char *text,
    int nText,
    const JsonStringArray *terms,
    const char *mode,
    int limit,
    SnippetSpan **spans,
    int *spanCount,
    int *spanCap,
    int *requirementsMet) {
  *requirementsMet = 1;
  if (text == NULL || terms == NULL || mode == NULL || nText <= 0 || limit <= 0) {
    *requirementsMet = 0;
    return SQLITE_OK;
  }

  SourceAnalyzedTokenBuffer tokens;
  int rc = collect_source_analyzed_tokens(text, nText, &tokens);
  if (rc != SQLITE_OK) {
    return rc;
  }

  if (strcmp(mode, "phrase") == 0) {
    int firstTerm = -1;
    int termCount = 0;
    for (int i = 0; i < terms->count; i++) {
      if (terms->lens[i] > 0) {
        if (firstTerm < 0) {
          firstTerm = i;
        }
        termCount++;
      }
    }
    if (termCount == 0 || termCount > tokens.count) {
      *requirementsMet = 0;
    } else {
      int emitted = 0;
      for (int i = 0; emitted < limit && i <= tokens.count - termCount; i++) {
        int termIndex = firstTerm;
        int matched = 1;
        for (int j = 0; j < termCount; j++) {
          while (termIndex < terms->count && terms->lens[termIndex] <= 0) {
            termIndex++;
          }
          if (termIndex >= terms->count || !source_token_matches_term(&tokens.items[i + j], terms->items[termIndex], terms->lens[termIndex])) {
            matched = 0;
            break;
          }
          termIndex++;
        }
        if (matched) {
          rc = append_snippet_span(spans, spanCount, spanCap, tokens.items[i].start, tokens.items[i + termCount - 1].end);
          if (rc != SQLITE_OK) {
            break;
          }
          emitted++;
        }
      }
      *requirementsMet = emitted > 0;
    }
  } else if (strcmp(mode, "any") == 0 || strcmp(mode, "all") == 0) {
    int requireAll = strcmp(mode, "all") == 0;
    int validTerms = 0;
    for (int termIdx = 0; rc == SQLITE_OK && termIdx < terms->count; termIdx++) {
      if (terms->lens[termIdx] <= 0) {
        continue;
      }
      validTerms++;
      int matchedThisTerm = 0;
      for (int tokenIdx = 0; matchedThisTerm < limit && tokenIdx < tokens.count; tokenIdx++) {
        if (source_token_matches_term(&tokens.items[tokenIdx], terms->items[termIdx], terms->lens[termIdx])) {
          rc = append_snippet_span(spans, spanCount, spanCap, tokens.items[tokenIdx].start, tokens.items[tokenIdx].end);
          if (rc != SQLITE_OK) {
            break;
          }
          matchedThisTerm++;
        }
      }
      if (requireAll && matchedThisTerm == 0) {
        *requirementsMet = 0;
        break;
      }
    }
    if (validTerms == 0 || (!requireAll && *spanCount == 0)) {
      *requirementsMet = 0;
    }
  } else {
    source_analyzed_token_buffer_free(&tokens);
    return SQLITE_MISUSE;
  }

  source_analyzed_token_buffer_free(&tokens);
  return rc;
}

static int render_spans_json(sqlite3_context *context, const SnippetSpan *spans, int spanCount) {
  sqlite3_str *json = sqlite3_str_new(sqlite3_context_db_handle(context));
  if (json == NULL) {
    return SQLITE_NOMEM;
  }
  sqlite3_str_appendall(json, "[");
  for (int i = 0; i < spanCount; i++) {
    if (i > 0) {
      sqlite3_str_appendall(json, ",");
    }
    sqlite3_str_appendf(json, "{\"start\":%d,\"end\":%d}", spans[i].start, spans[i].end);
  }
  sqlite3_str_appendall(json, "]");
  char *jsonOut = sqlite3_str_finish(json);
  if (jsonOut == NULL) {
    return SQLITE_NOMEM;
  }
  sqlite3_result_text(context, jsonOut, -1, sqlite3_free);
  return SQLITE_OK;
}

static void udf_sqlite_tokenizer_ar_find_all_analyzed_match_spans_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc < 3 || argc > 4) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_find_all_analyzed_match_spans_json expects 3..4 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL || sqlite3_value_type(argv[2]) == SQLITE_NULL) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }
  const char *text = (const char *)sqlite3_value_text(argv[0]);
  int nText = sqlite3_value_bytes(argv[0]);
  const char *termsJson = (const char *)sqlite3_value_text(argv[1]);
  int nTermsJson = sqlite3_value_bytes(argv[1]);
  const char *mode = (const char *)sqlite3_value_text(argv[2]);
  int limit = (argc >= 4) ? sqlite3_value_int(argv[3]) : 8;
  if (text == NULL || termsJson == NULL || mode == NULL || nText <= 0 || nTermsJson <= 0 || limit <= 0) {
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }

  JsonStringArray terms;
  int rc = parse_json_string_array(termsJson, nTermsJson, &terms);
  if (rc == SQLITE_NOMEM) {
    sqlite3_result_error_nomem(context);
    return;
  }
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, "invalid terms_json: expected JSON string array", -1);
    return;
  }

  SnippetSpan *candidates = NULL;
  int candidateCount = 0;
  int candidateCap = 0;
  int requirementsMet = 0;
  rc = collect_analyzed_match_spans(text, nText, &terms, mode, limit, &candidates, &candidateCount, &candidateCap, &requirementsMet);
  json_string_array_free(&terms);
  if (rc == SQLITE_MISUSE) {
    sqlite3_free(candidates);
    sqlite3_result_error(context, "mode must be one of: any, all, phrase", -1);
    return;
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(candidates);
    sqlite3_result_error_nomem(context);
    return;
  }
  if (!requirementsMet || candidateCount == 0) {
    sqlite3_free(candidates);
    sqlite3_result_text(context, "[]", -1, SQLITE_STATIC);
    return;
  }

  SnippetSpan *picked = NULL;
  int pickedCount = 0;
  rc = select_highlight_spans(candidates, candidateCount, &picked, &pickedCount);
  sqlite3_free(candidates);
  if (rc != SQLITE_OK) {
    sqlite3_result_error_nomem(context);
    return;
  }
  rc = render_spans_json(context, picked, pickedCount);
  sqlite3_free(picked);
  if (rc != SQLITE_OK) {
    sqlite3_result_error_nomem(context);
  }
}

static int honorific_expansion_matches_term(
    const HonorificExpansion *expansion,
    const NormalizeOptions *options,
    const uint32_t *termCodepoints,
    int termCount) {
  if (expansion == NULL || termCodepoints == NULL || termCount <= 0) {
    return 0;
  }
  uint32_t *phraseCodepoints = NULL;
  int *phraseMapUnused = NULL;
  int phraseCount = 0;
  int rc = normalize_to_codepoints_with_map(
      expansion->phrase, expansion->phraseLen, options, &phraseCodepoints, &phraseMapUnused, &phraseCount);
  sqlite3_free(phraseMapUnused);
  if (rc != SQLITE_OK) {
    sqlite3_free(phraseCodepoints);
    return -1;
  }
  int matched = find_codepoint_subsequence(phraseCodepoints, phraseCount, termCodepoints, termCount, 0) >= 0;
  sqlite3_free(phraseCodepoints);
  return matched;
}

static int collect_normalized_match_spans(
    const char *text,
    int nText,
    const char *term,
    int nTerm,
    int limit,
    SnippetSpan **spans,
    int *spanCount,
    int *spanCap,
    int *matchCount) {
  *matchCount = 0;
  if (text == NULL || term == NULL || nText <= 0 || nTerm <= 0 || limit <= 0) {
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

  if (textCount > 0 && termCount > 0 && termCount <= textCount) {
    int cursor = 0;
    while (*matchCount < limit && cursor < textCount) {
      int pos = find_codepoint_subsequence(textCodepoints, textCount, termCodepoints, termCount, cursor);
      if (pos < 0) {
        break;
      }
      rc = append_snippet_span(spans, spanCount, spanCap, textOrigIndexMap[pos], textOrigIndexMap[pos + termCount - 1] + 1);
      if (rc != SQLITE_OK) {
        break;
      }
      (*matchCount)++;
      cursor = pos + termCount;
    }
  }

  int bytePos = 0;
  int codepointPos = 0;
  while (*matchCount < limit && bytePos < nText) {
    int consumed = 0;
    uint32_t cp = 0;
    if (!utf8_decode(text + bytePos, nText - bytePos, &consumed, &cp)) {
      bytePos++;
      continue;
    }
    const HonorificExpansion *expansion = find_honorific_expansion(text + bytePos, nText - bytePos);
    if (expansion != NULL) {
      int matched = honorific_expansion_matches_term(expansion, &options, termCodepoints, termCount);
      if (matched < 0) {
        rc = SQLITE_NOMEM;
        break;
      }
      if (matched) {
        rc = append_snippet_span(spans, spanCount, spanCap, codepointPos, codepointPos + 1);
        if (rc != SQLITE_OK) {
          break;
        }
        (*matchCount)++;
      }
    }
    bytePos += consumed;
    codepointPos++;
  }

  sqlite3_free(textCodepoints);
  sqlite3_free(textOrigIndexMap);
  sqlite3_free(termCodepoints);
  return rc;
}

static int compare_highlight_spans(const void *left, const void *right) {
  const SnippetSpan *a = (const SnippetSpan *)left;
  const SnippetSpan *b = (const SnippetSpan *)right;
  if (a->start != b->start) {
    return (a->start < b->start) ? -1 : 1;
  }
  int lenA = a->end - a->start;
  int lenB = b->end - b->start;
  if (lenA != lenB) {
    return (lenA > lenB) ? -1 : 1;
  }
  if (a->end != b->end) {
    return (a->end < b->end) ? -1 : 1;
  }
  return 0;
}

static int select_highlight_spans(SnippetSpan *spans, int spanCount, SnippetSpan **outSpans, int *outCount) {
  *outSpans = NULL;
  *outCount = 0;
  if (spanCount <= 0) {
    return SQLITE_OK;
  }
  qsort(spans, (size_t)spanCount, sizeof(SnippetSpan), compare_highlight_spans);
  SnippetSpan *picked = ar_malloc_array(spanCount, sizeof(SnippetSpan));
  if (picked == NULL) {
    return SQLITE_NOMEM;
  }
  int pickedCount = 0;
  for (int i = 0; i < spanCount; i++) {
    int overlaps = 0;
    for (int j = 0; j < pickedCount; j++) {
      if (spans_overlap_inclusive(spans[i].start, spans[i].end, picked[j].start, picked[j].end)) {
        overlaps = 1;
        break;
      }
    }
    if (!overlaps) {
      picked[pickedCount++] = spans[i];
    }
  }
  qsort(picked, (size_t)pickedCount, sizeof(SnippetSpan), compare_spans_asc);
  *outSpans = picked;
  *outCount = pickedCount;
  return SQLITE_OK;
}

static int join_json_terms_with_space(const JsonStringArray *terms, char **out, int *outLen) {
  int total = 0;
  int valid = 0;
  for (int i = 0; i < terms->count; i++) {
    if (terms->lens[i] <= 0) {
      continue;
    }
    int add = 0;
    if (ar_checked_int_sum(terms->lens[i], valid > 0 ? 1 : 0, &add) != SQLITE_OK || ar_checked_int_sum(total, add, &total) != SQLITE_OK) {
      return SQLITE_TOOBIG;
    }
    valid++;
  }
  if (valid == 0) {
    *out = NULL;
    *outLen = 0;
    return SQLITE_OK;
  }
  int joinedCap = 0;
  if (ar_checked_int_sum(total, 1, &joinedCap) != SQLITE_OK) {
    return SQLITE_TOOBIG;
  }
  char *joined = sqlite3_malloc64((sqlite3_uint64)joinedCap);
  if (joined == NULL) {
    return SQLITE_NOMEM;
  }
  int pos = 0;
  valid = 0;
  for (int i = 0; i < terms->count; i++) {
    if (terms->lens[i] <= 0) {
      continue;
    }
    if (valid > 0) {
      joined[pos++] = ' ';
    }
    memcpy(joined + pos, terms->items[i], (size_t)terms->lens[i]);
    pos += terms->lens[i];
    valid++;
  }
  joined[pos] = '\0';
  *out = joined;
  *outLen = pos;
  return SQLITE_OK;
}

static int render_text_with_marker_spans(
    const char *text,
    int nText,
    const SnippetSpan *spans,
    int spanCount,
    const char *startMarker,
    int nStartMarker,
    const char *endMarker,
    int nEndMarker,
    char **outText,
    int *outLen) {
  uint32_t *textCps = NULL;
  uint32_t *startCps = NULL;
  uint32_t *endCps = NULL;
  int textLen = 0;
  int startLen = 0;
  int endLen = 0;
  int rc = decode_text_to_codepoints(text, nText, &textCps, &textLen);
  if (rc != SQLITE_OK) {
    return rc;
  }
  rc = decode_text_to_codepoints(startMarker, nStartMarker, &startCps, &startLen);
  if (rc != SQLITE_OK) {
    sqlite3_free(textCps);
    return rc;
  }
  rc = decode_text_to_codepoints(endMarker, nEndMarker, &endCps, &endLen);
  if (rc != SQLITE_OK) {
    sqlite3_free(textCps);
    sqlite3_free(startCps);
    return rc;
  }

  int markerLen = 0;
  if (ar_checked_int_sum(startLen, endLen, &markerLen) != SQLITE_OK) {
    sqlite3_free(textCps);
    sqlite3_free(startCps);
    sqlite3_free(endCps);
    return SQLITE_TOOBIG;
  }
  sqlite3_int64 markerTotal = (sqlite3_int64)spanCount * markerLen;
  int textAndMarkers = 0;
  if (ar_checked_int_sum(textLen, markerTotal, &textAndMarkers) != SQLITE_OK) {
    sqlite3_free(textCps);
    sqlite3_free(startCps);
    sqlite3_free(endCps);
    return SQLITE_TOOBIG;
  }
  int outCap = 0;
  if (ar_checked_int_sum(textAndMarkers, 4, &outCap) != SQLITE_OK || outCap < 4) {
    sqlite3_free(textCps);
    sqlite3_free(startCps);
    sqlite3_free(endCps);
    return SQLITE_TOOBIG;
  }
  uint32_t *outCps = ar_malloc_array(outCap, sizeof(uint32_t));
  if (outCps == NULL) {
    sqlite3_free(textCps);
    sqlite3_free(startCps);
    sqlite3_free(endCps);
    return SQLITE_NOMEM;
  }

  int outPos = 0;
  int cursor = 0;
  for (int i = 0; i < spanCount; i++) {
    int start = spans[i].start;
    int end = spans[i].end;
    if (start < cursor) {
      start = cursor;
    }
    if (start > textLen) {
      start = textLen;
    }
    if (end < start) {
      end = start;
    }
    if (end > textLen) {
      end = textLen;
    }
    memcpy(outCps + outPos, textCps + cursor, (size_t)(start - cursor) * sizeof(uint32_t));
    outPos += start - cursor;
    memcpy(outCps + outPos, startCps, (size_t)startLen * sizeof(uint32_t));
    outPos += startLen;
    memcpy(outCps + outPos, textCps + start, (size_t)(end - start) * sizeof(uint32_t));
    outPos += end - start;
    memcpy(outCps + outPos, endCps, (size_t)endLen * sizeof(uint32_t));
    outPos += endLen;
    cursor = end;
  }
  memcpy(outCps + outPos, textCps + cursor, (size_t)(textLen - cursor) * sizeof(uint32_t));
  outPos += textLen - cursor;

  rc = encode_codepoints_to_utf8_text(outCps, outPos, outText, outLen);
  sqlite3_free(textCps);
  sqlite3_free(startCps);
  sqlite3_free(endCps);
  sqlite3_free(outCps);
  return rc;
}

static void udf_sqlite_tokenizer_ar_highlight_normalized_matches(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 6) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_highlight_normalized_matches expects 6 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL || sqlite3_value_type(argv[2]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }

  const char *text = (const char *)sqlite3_value_text(argv[0]);
  int nText = sqlite3_value_bytes(argv[0]);
  const char *termsJson = (const char *)sqlite3_value_text(argv[1]);
  int nTermsJson = sqlite3_value_bytes(argv[1]);
  const char *mode = (const char *)sqlite3_value_text(argv[2]);
  const char *startMarker = sqlite3_value_type(argv[3]) == SQLITE_NULL ? "" : (const char *)sqlite3_value_text(argv[3]);
  int nStartMarker = sqlite3_value_type(argv[3]) == SQLITE_NULL ? 0 : sqlite3_value_bytes(argv[3]);
  const char *endMarker = sqlite3_value_type(argv[4]) == SQLITE_NULL ? "" : (const char *)sqlite3_value_text(argv[4]);
  int nEndMarker = sqlite3_value_type(argv[4]) == SQLITE_NULL ? 0 : sqlite3_value_bytes(argv[4]);
  int limit = sqlite3_value_int(argv[5]);
  if (text == NULL || termsJson == NULL || mode == NULL || nText <= 0 || nTermsJson <= 0 || limit <= 0) {
    sqlite3_result_null(context);
    return;
  }

  JsonStringArray terms;
  int rc = parse_json_string_array(termsJson, nTermsJson, &terms);
  if (rc == SQLITE_NOMEM) {
    sqlite3_result_error_nomem(context);
    return;
  }
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, "invalid terms_json: expected JSON string array", -1);
    return;
  }

  SnippetSpan *candidates = NULL;
  int candidateCount = 0;
  int candidateCap = 0;
  int validTerms = 0;
  int requirementsMet = 1;

  if (strcmp(mode, "phrase") == 0) {
    char *phrase = NULL;
    int nPhrase = 0;
    if (terms.count == 1 && terms.lens[0] > 0) {
      int phraseCap = 0;
      if (ar_checked_int_sum(terms.lens[0], 1, &phraseCap) != SQLITE_OK) {
        json_string_array_free(&terms);
        sqlite3_result_error_toobig(context);
        return;
      }
      phrase = sqlite3_malloc64((sqlite3_uint64)phraseCap);
      if (phrase == NULL) {
        json_string_array_free(&terms);
        sqlite3_result_error_nomem(context);
        return;
      }
      memcpy(phrase, terms.items[0], (size_t)terms.lens[0]);
      phrase[terms.lens[0]] = '\0';
      nPhrase = terms.lens[0];
    } else {
      rc = join_json_terms_with_space(&terms, &phrase, &nPhrase);
      if (rc != SQLITE_OK) {
        json_string_array_free(&terms);
        sqlite3_result_error_nomem(context);
        return;
      }
    }
    if (nPhrase <= 0 || phrase == NULL) {
      requirementsMet = 0;
    } else {
      int matchCount = 0;
      rc = collect_normalized_match_spans(text, nText, phrase, nPhrase, limit, &candidates, &candidateCount, &candidateCap, &matchCount);
      if (rc != SQLITE_OK) {
        sqlite3_free(phrase);
        sqlite3_free(candidates);
        json_string_array_free(&terms);
        sqlite3_result_error_nomem(context);
        return;
      }
      requirementsMet = matchCount > 0;
    }
    sqlite3_free(phrase);
  } else if (strcmp(mode, "any") == 0 || strcmp(mode, "all") == 0) {
    int requireAll = strcmp(mode, "all") == 0;
    for (int i = 0; i < terms.count; i++) {
      if (terms.lens[i] <= 0) {
        continue;
      }
      validTerms++;
      int matchCount = 0;
      rc = collect_normalized_match_spans(
          text, nText, terms.items[i], terms.lens[i], limit, &candidates, &candidateCount, &candidateCap, &matchCount);
      if (rc != SQLITE_OK) {
        sqlite3_free(candidates);
        json_string_array_free(&terms);
        sqlite3_result_error_nomem(context);
        return;
      }
      if (requireAll && matchCount == 0) {
        requirementsMet = 0;
        break;
      }
    }
    if (validTerms == 0 || (!requireAll && candidateCount == 0)) {
      requirementsMet = 0;
    }
  } else {
    sqlite3_free(candidates);
    json_string_array_free(&terms);
    sqlite3_result_error(context, "mode must be one of: any, all, phrase", -1);
    return;
  }

  json_string_array_free(&terms);
  if (!requirementsMet || candidateCount == 0) {
    sqlite3_free(candidates);
    sqlite3_result_null(context);
    return;
  }

  SnippetSpan *picked = NULL;
  int pickedCount = 0;
  rc = select_highlight_spans(candidates, candidateCount, &picked, &pickedCount);
  sqlite3_free(candidates);
  if (rc != SQLITE_OK) {
    sqlite3_result_error_nomem(context);
    return;
  }
  if (pickedCount == 0) {
    sqlite3_free(picked);
    sqlite3_result_null(context);
    return;
  }

  char *out = NULL;
  int nOut = 0;
  rc = render_text_with_marker_spans(text, nText, picked, pickedCount, startMarker, nStartMarker, endMarker, nEndMarker, &out, &nOut);
  sqlite3_free(picked);
  if (rc != SQLITE_OK) {
    sqlite3_result_error_nomem(context);
    return;
  }
  sqlite3_result_text(context, out, nOut, sqlite3_free);
}

static void udf_sqlite_tokenizer_ar_highlight_analyzed_matches(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc < 6 || argc > 7) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_highlight_analyzed_matches expects 6..7 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL || sqlite3_value_type(argv[2]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }

  const char *text = (const char *)sqlite3_value_text(argv[0]);
  int nText = sqlite3_value_bytes(argv[0]);
  const char *termsJson = (const char *)sqlite3_value_text(argv[1]);
  int nTermsJson = sqlite3_value_bytes(argv[1]);
  const char *mode = (const char *)sqlite3_value_text(argv[2]);
  const char *startMarker = sqlite3_value_type(argv[3]) == SQLITE_NULL ? "" : (const char *)sqlite3_value_text(argv[3]);
  int nStartMarker = sqlite3_value_type(argv[3]) == SQLITE_NULL ? 0 : sqlite3_value_bytes(argv[3]);
  const char *endMarker = sqlite3_value_type(argv[4]) == SQLITE_NULL ? "" : (const char *)sqlite3_value_text(argv[4]);
  int nEndMarker = sqlite3_value_type(argv[4]) == SQLITE_NULL ? 0 : sqlite3_value_bytes(argv[4]);
  int limit = sqlite3_value_int(argv[5]);
  const char *rawTermsJson = (argc >= 7 && sqlite3_value_type(argv[6]) != SQLITE_NULL) ? (const char *)sqlite3_value_text(argv[6]) : NULL;
  int nRawTermsJson = (argc >= 7 && sqlite3_value_type(argv[6]) != SQLITE_NULL) ? sqlite3_value_bytes(argv[6]) : 0;
  if (text == NULL || termsJson == NULL || mode == NULL || nText <= 0 || nTermsJson <= 0 || limit <= 0) {
    sqlite3_result_null(context);
    return;
  }

  JsonStringArray terms;
  int rc = parse_json_string_array(termsJson, nTermsJson, &terms);
  if (rc == SQLITE_NOMEM) {
    sqlite3_result_error_nomem(context);
    return;
  }
  if (rc != SQLITE_OK) {
    sqlite3_result_error(context, "invalid terms_json: expected JSON string array", -1);
    return;
  }

  SnippetSpan *candidates = NULL;
  int candidateCount = 0;
  int candidateCap = 0;
  int requirementsMet = 0;
  rc = collect_analyzed_match_spans(text, nText, &terms, mode, limit, &candidates, &candidateCount, &candidateCap, &requirementsMet);
  json_string_array_free(&terms);
  if (rc == SQLITE_MISUSE) {
    sqlite3_free(candidates);
    sqlite3_result_error(context, "mode must be one of: any, all, phrase", -1);
    return;
  }
  if (rc != SQLITE_OK) {
    sqlite3_free(candidates);
    sqlite3_result_error_nomem(context);
    return;
  }
  if (rawTermsJson != NULL && nRawTermsJson > 0) {
    JsonStringArray rawTerms;
    rc = parse_json_string_array(rawTermsJson, nRawTermsJson, &rawTerms);
    if (rc == SQLITE_NOMEM) {
      sqlite3_free(candidates);
      sqlite3_result_error_nomem(context);
      return;
    }
    if (rc != SQLITE_OK) {
      sqlite3_free(candidates);
      sqlite3_result_error(context, "invalid raw_terms_json: expected JSON string array", -1);
      return;
    }
    int rawMatches = 0;
    if (strcmp(mode, "phrase") == 0) {
      char *phrase = NULL;
      int nPhrase = 0;
      if (rawTerms.count == 1 && rawTerms.lens[0] > 0) {
        int phraseCap = 0;
        if (ar_checked_int_sum(rawTerms.lens[0], 1, &phraseCap) != SQLITE_OK) {
          json_string_array_free(&rawTerms);
          sqlite3_free(candidates);
          sqlite3_result_error_toobig(context);
          return;
        }
        phrase = sqlite3_malloc64((sqlite3_uint64)phraseCap);
        if (phrase == NULL) {
          json_string_array_free(&rawTerms);
          sqlite3_free(candidates);
          sqlite3_result_error_nomem(context);
          return;
        }
        memcpy(phrase, rawTerms.items[0], (size_t)rawTerms.lens[0]);
        phrase[rawTerms.lens[0]] = '\0';
        nPhrase = rawTerms.lens[0];
      } else {
        rc = join_json_terms_with_space(&rawTerms, &phrase, &nPhrase);
      }
      if (rc == SQLITE_OK && phrase != NULL && nPhrase > 0) {
        rc = collect_normalized_match_spans(text, nText, phrase, nPhrase, limit, &candidates, &candidateCount, &candidateCap, &rawMatches);
      }
      sqlite3_free(phrase);
    } else if (strcmp(mode, "any") == 0 || strcmp(mode, "all") == 0) {
      for (int i = 0; rc == SQLITE_OK && i < rawTerms.count; i++) {
        if (rawTerms.lens[i] <= 0) {
          continue;
        }
        int termMatches = 0;
        rc = collect_normalized_match_spans(
            text, nText, rawTerms.items[i], rawTerms.lens[i], limit, &candidates, &candidateCount, &candidateCap, &termMatches);
        rawMatches += termMatches;
      }
    } else {
      rc = SQLITE_MISUSE;
    }
    json_string_array_free(&rawTerms);
    if (rc == SQLITE_MISUSE) {
      sqlite3_free(candidates);
      sqlite3_result_error(context, "mode must be one of: any, all, phrase", -1);
      return;
    }
    if (rc != SQLITE_OK) {
      sqlite3_free(candidates);
      sqlite3_result_error_nomem(context);
      return;
    }
    if (rawMatches > 0) {
      requirementsMet = 1;
    }
  }
  if (!requirementsMet || candidateCount == 0) {
    sqlite3_free(candidates);
    sqlite3_result_null(context);
    return;
  }

  SnippetSpan *picked = NULL;
  int pickedCount = 0;
  rc = select_highlight_spans(candidates, candidateCount, &picked, &pickedCount);
  sqlite3_free(candidates);
  if (rc != SQLITE_OK) {
    sqlite3_result_error_nomem(context);
    return;
  }
  if (pickedCount == 0) {
    sqlite3_free(picked);
    sqlite3_result_null(context);
    return;
  }

  char *out = NULL;
  int nOut = 0;
  rc = render_text_with_marker_spans(text, nText, picked, pickedCount, startMarker, nStartMarker, endMarker, nEndMarker, &out, &nOut);
  sqlite3_free(picked);
  if (rc != SQLITE_OK) {
    sqlite3_result_error_nomem(context);
    return;
  }
  sqlite3_result_text(context, out, nOut, sqlite3_free);
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
  int snippetCap = 0;
  if (ar_checked_int_sum(snippetLen, 16, &snippetCap) != SQLITE_OK || snippetCap < 16) {
    sqlite3_free(textCodepoints);
    sqlite3_result_error_toobig(context);
    return;
  }
  uint32_t *snippet = ar_malloc_array(snippetCap, sizeof(uint32_t));
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
  SnippetSpan *spans = ar_malloc_array(spanCap, sizeof(SnippetSpan));
  if (spans == NULL) {
    sqlite3_free(textCodepoints);
    sqlite3_free(snippet);
    sqlite3_result_error_nomem(context);
    return;
  }
  if (spansCsv != NULL && nSpansCsv > 0) {
    int spansCopyCap = 0;
    if (ar_checked_int_sum(nSpansCsv, 1, &spansCopyCap) != SQLITE_OK) {
      sqlite3_free(textCodepoints);
      sqlite3_free(snippet);
      sqlite3_free(spans);
      sqlite3_result_error_toobig(context);
      return;
    }
    char *copy = sqlite3_malloc64((sqlite3_uint64)spansCopyCap);
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
          int nextCap = 0;
          int growRc = ar_grow_capacity(spanCap, spanCount + 1, 8, &nextCap);
          if (growRc != SQLITE_OK) {
            sqlite3_free(copy);
            sqlite3_free(textCodepoints);
            sqlite3_free(snippet);
            sqlite3_free(spans);
            sqlite3_result_error_toobig(context);
            return;
          }
          SnippetSpan *next = ar_realloc_array(spans, nextCap, sizeof(SnippetSpan));
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

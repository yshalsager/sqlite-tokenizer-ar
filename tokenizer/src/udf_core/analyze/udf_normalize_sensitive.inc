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

static int json_string_array_append(JsonStringArray *array, char *value, int nValue) {
  if (array->count >= array->cap) {
    int nextCap = array->cap == 0 ? 4 : array->cap * 2;
    char **nextItems = sqlite3_realloc(array->items, (size_t)nextCap * sizeof(char *));
    if (nextItems == NULL) {
      return SQLITE_NOMEM;
    }
    array->items = nextItems;
    int *nextLens = sqlite3_realloc(array->lens, (size_t)nextCap * sizeof(int));
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
  char *value = sqlite3_malloc((size_t)cap);
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
    if (c == '\\') {
      if (*pos >= nJson) {
        sqlite3_free(value);
        return SQLITE_ERROR;
      }
      c = (unsigned char)json[*pos];
      (*pos)++;
      if (c == '"' || c == '\\' || c == '/') {
      } else if (c == 'b') {
        c = '\b';
      } else if (c == 'f') {
        c = '\f';
      } else if (c == 'n') {
        c = '\n';
      } else if (c == 'r') {
        c = '\r';
      } else if (c == 't') {
        c = '\t';
      } else if (c == 'u') {
        if (*pos + 4 > nJson) {
          sqlite3_free(value);
          return SQLITE_ERROR;
        }
        uint32_t cp = 0;
        for (int i = 0; i < 4; i++) {
          int digit = json_hex_digit_value(json[*pos + i]);
          if (digit < 0) {
            sqlite3_free(value);
            return SQLITE_ERROR;
          }
          cp = (cp << 4) | (uint32_t)digit;
        }
        *pos += 4;
        char encoded[4];
        int encodedLen = utf8_encode(cp, encoded);
        if (outPos + encodedLen + 1 > cap) {
          int nextCap = cap * 2;
          if (nextCap < outPos + encodedLen + 1) {
            nextCap = outPos + encodedLen + 1;
          }
          char *next = sqlite3_realloc(value, (size_t)nextCap);
          if (next == NULL) {
            sqlite3_free(value);
            return SQLITE_NOMEM;
          }
          value = next;
          cap = nextCap;
        }
        memcpy(value + outPos, encoded, (size_t)encodedLen);
        outPos += encodedLen;
        continue;
      } else {
        sqlite3_free(value);
        return SQLITE_ERROR;
      }
    }
    if (outPos + 2 > cap) {
      int nextCap = cap * 2;
      char *next = sqlite3_realloc(value, (size_t)nextCap);
      if (next == NULL) {
        sqlite3_free(value);
        return SQLITE_NOMEM;
      }
      value = next;
      cap = nextCap;
    }
    value[outPos++] = (char)c;
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
    int nextCap = *cap == 0 ? 8 : *cap * 2;
    SnippetSpan *next = sqlite3_realloc(*spans, (size_t)nextCap * sizeof(SnippetSpan));
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
  SnippetSpan *picked = sqlite3_malloc((size_t)spanCount * sizeof(SnippetSpan));
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
    total += terms->lens[i] + (valid > 0 ? 1 : 0);
    valid++;
  }
  if (valid == 0) {
    *out = NULL;
    *outLen = 0;
    return SQLITE_OK;
  }
  char *joined = sqlite3_malloc((size_t)total + 1);
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

  int outCap = textLen + spanCount * (startLen + endLen) + 4;
  if (outCap < 4) {
    outCap = 4;
  }
  uint32_t *outCps = sqlite3_malloc((size_t)outCap * sizeof(uint32_t));
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
      phrase = sqlite3_malloc((size_t)terms.lens[0] + 1);
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

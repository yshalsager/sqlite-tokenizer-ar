typedef struct PlanQueryOptions {
  int allowPrefix;
  int allowSuffix;
  int allowWildcard;
  int allowFuzzy;
  int ignoreDiacritics;
  int ignoreHamzaForms;
  int ignoreLetterForms;
  int ignoreDigitForms;
  int lenientParseErrors;
  int suffixMax;
  int wildcardMax;
  int fuzzyMax;
  JsonStringArray tokenizerArgs;
} PlanQueryOptions;

typedef struct PlanQueryContext {
  ArabicTokenizer *tokenizer;
  PlanQueryOptions options;
  const char *field;
  int nField;
  int expansionId;
  char *expandJson;
  int nExpandJson;
  int expandJsonCap;
  int expandCount;
} PlanQueryContext;

static void plan_query_options_init(PlanQueryOptions *options) {
  memset(options, 0, sizeof(*options));
  options->allowPrefix = 1;
  options->allowSuffix = 1;
  options->allowWildcard = 1;
  options->allowFuzzy = 1;
  options->ignoreDiacritics = 1;
  options->ignoreHamzaForms = 1;
  options->ignoreLetterForms = 1;
  options->ignoreDigitForms = 1;
  options->suffixMax = 256;
  options->wildcardMax = 256;
  options->fuzzyMax = 128;
}

static int plan_query_parse_bool(const char *json, int nJson, int *pos, int *out) {
  if (*pos + 4 <= nJson && memcmp(json + *pos, "true", 4) == 0) {
    *pos += 4;
    *out = 1;
    return SQLITE_OK;
  }
  if (*pos + 5 <= nJson && memcmp(json + *pos, "false", 5) == 0) {
    *pos += 5;
    *out = 0;
    return SQLITE_OK;
  }
  return SQLITE_ERROR;
}

static int plan_query_parse_positive_int(const char *json, int nJson, int *pos, int *out) {
  int start = *pos;
  while (*pos < nJson && json[*pos] >= '0' && json[*pos] <= '9') {
    (*pos)++;
  }
  if (*pos == start || !parse_nonnegative_int_span(json + start, *pos - start, INT_MAX, out) || *out < 1) {
    return SQLITE_ERROR;
  }
  return SQLITE_OK;
}

static int plan_query_parse_string_array_at(const char *json, int nJson, int *pos, JsonStringArray *out) {
  if (*pos >= nJson || json[*pos] != '[') {
    return SQLITE_ERROR;
  }
  (*pos)++;
  *pos = json_skip_ws(json, nJson, *pos);
  if (*pos < nJson && json[*pos] == ']') {
    (*pos)++;
    return SQLITE_OK;
  }
  while (*pos < nJson) {
    char *value = NULL;
    int nValue = 0;
    int rc = parse_json_string_value(json, nJson, pos, &value, &nValue);
    if (rc != SQLITE_OK) {
      return rc;
    }
    rc = json_string_array_append(out, value, nValue);
    if (rc != SQLITE_OK) {
      sqlite3_free(value);
      return rc;
    }
    *pos = json_skip_ws(json, nJson, *pos);
    if (*pos < nJson && json[*pos] == ',') {
      (*pos)++;
      *pos = json_skip_ws(json, nJson, *pos);
      continue;
    }
    if (*pos < nJson && json[*pos] == ']') {
      (*pos)++;
      return SQLITE_OK;
    }
    return SQLITE_ERROR;
  }
  return SQLITE_ERROR;
}

static int plan_query_key_equals(const char *key, int nKey, const char *expected) {
  return nKey == (int)strlen(expected) && memcmp(key, expected, (size_t)nKey) == 0;
}

static int plan_query_parse_options(const char *json, int nJson, PlanQueryOptions *options) {
  plan_query_options_init(options);
  if (json == NULL || nJson <= 0) {
    return SQLITE_OK;
  }
  int pos = json_skip_ws(json, nJson, 0);
  if (pos >= nJson || json[pos] != '{') {
    return SQLITE_ERROR;
  }
  pos++;
  pos = json_skip_ws(json, nJson, pos);
  if (pos < nJson && json[pos] == '}') {
    pos++;
    return json_skip_ws(json, nJson, pos) == nJson ? SQLITE_OK : SQLITE_ERROR;
  }
  while (pos < nJson) {
    char *key = NULL;
    int nKey = 0;
    int rc = parse_json_string_value(json, nJson, &pos, &key, &nKey);
    if (rc != SQLITE_OK) {
      sqlite3_free(key);
      return rc;
    }
    pos = json_skip_ws(json, nJson, pos);
    if (pos >= nJson || json[pos++] != ':') {
      sqlite3_free(key);
      return SQLITE_ERROR;
    }
    pos = json_skip_ws(json, nJson, pos);
    if (plan_query_key_equals(key, nKey, "tokenizer_args")) {
      rc = plan_query_parse_string_array_at(json, nJson, &pos, &options->tokenizerArgs);
    } else if (plan_query_key_equals(key, nKey, "allow_prefix_search")) {
      rc = plan_query_parse_bool(json, nJson, &pos, &options->allowPrefix);
    } else if (plan_query_key_equals(key, nKey, "allow_suffix_search")) {
      rc = plan_query_parse_bool(json, nJson, &pos, &options->allowSuffix);
    } else if (plan_query_key_equals(key, nKey, "allow_wildcard_search")) {
      rc = plan_query_parse_bool(json, nJson, &pos, &options->allowWildcard);
    } else if (plan_query_key_equals(key, nKey, "allow_fuzzy_search")) {
      rc = plan_query_parse_bool(json, nJson, &pos, &options->allowFuzzy);
    } else if (plan_query_key_equals(key, nKey, "ignore_diacritics")) {
      rc = plan_query_parse_bool(json, nJson, &pos, &options->ignoreDiacritics);
    } else if (plan_query_key_equals(key, nKey, "ignore_hamza_forms")) {
      rc = plan_query_parse_bool(json, nJson, &pos, &options->ignoreHamzaForms);
    } else if (plan_query_key_equals(key, nKey, "ignore_letter_forms")) {
      rc = plan_query_parse_bool(json, nJson, &pos, &options->ignoreLetterForms);
    } else if (plan_query_key_equals(key, nKey, "ignore_digit_forms")) {
      rc = plan_query_parse_bool(json, nJson, &pos, &options->ignoreDigitForms);
    } else if (plan_query_key_equals(key, nKey, "lenient_parse_errors")) {
      rc = plan_query_parse_bool(json, nJson, &pos, &options->lenientParseErrors);
    } else if (plan_query_key_equals(key, nKey, "suffix_max_expansions")) {
      rc = plan_query_parse_positive_int(json, nJson, &pos, &options->suffixMax);
    } else if (plan_query_key_equals(key, nKey, "wildcard_max_expansions")) {
      rc = plan_query_parse_positive_int(json, nJson, &pos, &options->wildcardMax);
    } else if (plan_query_key_equals(key, nKey, "fuzzy_max_expansions")) {
      rc = plan_query_parse_positive_int(json, nJson, &pos, &options->fuzzyMax);
    } else {
      rc = SQLITE_ERROR;
    }
    sqlite3_free(key);
    if (rc != SQLITE_OK) {
      return rc;
    }
    pos = json_skip_ws(json, nJson, pos);
    if (pos < nJson && json[pos] == ',') {
      pos++;
      pos = json_skip_ws(json, nJson, pos);
      continue;
    }
    if (pos < nJson && json[pos] == '}') {
      pos++;
      return json_skip_ws(json, nJson, pos) == nJson ? SQLITE_OK : SQLITE_ERROR;
    }
    return SQLITE_ERROR;
  }
  return SQLITE_ERROR;
}


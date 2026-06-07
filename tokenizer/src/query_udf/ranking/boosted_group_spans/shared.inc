static int parse_runtime_field_for_boosted_group(
    sqlite3_context *context,
    const char *runtimeFieldRaw,
    int nRuntimeFieldRaw,
    const char **runtimeFieldOut,
    int *nRuntimeFieldOut) {
  if (token_equals_ascii_ci(runtimeFieldRaw, nRuntimeFieldRaw, "page")) {
    *runtimeFieldOut = "page";
    *nRuntimeFieldOut = 4;
    return 1;
  }
  if (token_equals_ascii_ci(runtimeFieldRaw, nRuntimeFieldRaw, "title")) {
    *runtimeFieldOut = "title";
    *nRuntimeFieldOut = 5;
    return 1;
  }
  sqlite3_result_null(context);
  return 0;
}

static int trim_ascii_space_bounds(const char *text, int nText, int *startOut, int *endOut) {
  int start = 0;
  int end = nText;
  while (start < end && isspace((unsigned char)text[start])) {
    start++;
  }
  while (end > start && isspace((unsigned char)text[end - 1])) {
    end--;
  }
  *startOut = start;
  *endOut = end;
  return end > start;
}

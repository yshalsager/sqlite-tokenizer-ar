static int is_arabic_diacritic_strict(uint32_t cp) {
  return (cp >= 0x064B && cp <= 0x065F) || (cp >= 0x06D6 && cp <= 0x06ED);
}

static uint32_t normalize_codepoint_with_options(uint32_t cp, const NormalizeOptions *options) {
  if (options->keepWildcards && (cp == '*' || cp == '?')) {
    return cp;
  }
  if (options->lowercase && cp >= 'A' && cp <= 'Z') {
    cp += 32;
  }
  if (options->ignoreDiacritics && (cp == 0x0640 || is_arabic_diacritic_strict(cp))) {
    return 0;
  }
  if (options->ignoreHamzaForms && (cp == 0x0622 || cp == 0x0623 || cp == 0x0625)) {
    return 0x0627;
  }
  if (options->ignoreLetterForms) {
    if (cp == 0x0649) {
      return 0x064A;
    }
    if (cp == 0x0629) {
      return 0x0647;
    }
  }
  if (options->ignoreDigitForms) {
    if (cp >= 0x0660 && cp <= 0x0669) {
      return (uint32_t)('0' + (cp - 0x0660));
    }
    if (cp >= 0x06F0 && cp <= 0x06F9) {
      return (uint32_t)('0' + (cp - 0x06F0));
    }
  }
  return cp;
}

static int normalize_text_with_options(
    const char *input,
    int nInput,
    const NormalizeOptions *options,
    char **zOut,
    int *nOut) {
  int cap = (nInput <= 0) ? 4 : (nInput * 4 + 4);
  char *out = sqlite3_malloc((size_t)cap);
  if (out == NULL) {
    return SQLITE_NOMEM;
  }
  int inPos = 0;
  int outPos = 0;
  while (inPos < nInput) {
    int consumed = 0;
    uint32_t cp = 0;
    if (!utf8_decode(input + inPos, nInput - inPos, &consumed, &cp)) {
      inPos++;
      continue;
    }
    inPos += consumed;
    cp = normalize_codepoint_with_options(cp, options);
    if (cp == 0) {
      continue;
    }
    char encoded[4];
    int encodedLen = utf8_encode(cp, encoded);
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
  *zOut = out;
  *nOut = outPos;
  return SQLITE_OK;
}

static int sqlite_value_bool(sqlite3_value *value, int defaultValue) {
  if (value == NULL || sqlite3_value_type(value) == SQLITE_NULL) {
    return defaultValue;
  }
  return sqlite3_value_int(value) != 0;
}


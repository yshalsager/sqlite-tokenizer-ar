static int bytes_contains(const char *haystack, int nHaystack, const char *needle, int nNeedle) {
  if (nNeedle <= 0) {
    return 1;
  }
  if (nHaystack <= 0 || nNeedle > nHaystack) {
    return 0;
  }
  int limit = nHaystack - nNeedle;
  for (int i = 0; i <= limit; i++) {
    if (memcmp(haystack + i, needle, (size_t)nNeedle) == 0) {
      return 1;
    }
  }
  return 0;
}

static int wildcard_has_meta(const char *pattern, int nPattern) {
  for (int i = 0; i < nPattern; i++) {
    if (pattern[i] == '*' || pattern[i] == '?') {
      return 1;
    }
  }
  return 0;
}

static int utf8_to_codepoints(const char *in, int nIn, uint32_t *out, int outCap) {
  int inPos = 0;
  int outPos = 0;
  while (inPos < nIn && outPos < outCap) {
    int consumed = 0;
    uint32_t cp = 0;
    if (!utf8_decode(in + inPos, nIn - inPos, &consumed, &cp)) {
      inPos++;
      continue;
    }
    inPos += consumed;
    out[outPos++] = cp;
  }
  return outPos;
}

static int codepoints_to_utf8(const uint32_t *in, int nIn, char *out, int outCap) {
  int outPos = 0;
  for (int i = 0; i < nIn; i++) {
    char encoded[4];
    int encodedLen = utf8_encode(in[i], encoded);
    if (outPos + encodedLen > outCap) {
      break;
    }
    memcpy(out + outPos, encoded, encodedLen);
    outPos += encodedLen;
  }
  return outPos;
}

static int wildcard_full_match_codepoints(const uint32_t *text, int nText, const uint32_t *pattern, int nPattern) {
  int textIdx = 0;
  int patIdx = 0;
  int starPatIdx = -1;
  int starTextIdx = -1;
  while (textIdx < nText) {
    if (patIdx < nPattern && (pattern[patIdx] == '?' || pattern[patIdx] == text[textIdx])) {
      textIdx++;
      patIdx++;
      continue;
    }
    if (patIdx < nPattern && pattern[patIdx] == '*') {
      starPatIdx = patIdx;
      starTextIdx = textIdx;
      patIdx++;
      continue;
    }
    if (starPatIdx >= 0) {
      patIdx = starPatIdx + 1;
      starTextIdx++;
      textIdx = starTextIdx;
      continue;
    }
    return 0;
  }
  while (patIdx < nPattern && pattern[patIdx] == '*') {
    patIdx++;
  }
  return patIdx == nPattern;
}

static int wildcard_search_codepoints(const uint32_t *text, int nText, const uint32_t *pattern, int nPattern) {
  if (nPattern <= 0) {
    return 1;
  }
  uint32_t *wrappedPattern = sqlite3_malloc((size_t)(nPattern + 2) * sizeof(uint32_t));
  if (wrappedPattern == NULL) {
    return -1;
  }
  wrappedPattern[0] = '*';
  memcpy(wrappedPattern + 1, pattern, (size_t)nPattern * sizeof(uint32_t));
  wrappedPattern[nPattern + 1] = '*';
  int matched = wildcard_full_match_codepoints(text, nText, wrappedPattern, nPattern + 2);
  sqlite3_free(wrappedPattern);
  return matched;
}


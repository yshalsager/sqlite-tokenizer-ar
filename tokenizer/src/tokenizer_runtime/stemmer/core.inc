static int starts_with_check_length(const uint32_t *s, int len, const uint32_t *prefix, int prefixLen) {
  if (prefixLen == 1 && len < 4) {
    return 0;
  }
  if (len < prefixLen + 2) {
    return 0;
  }
  for (int i = 0; i < prefixLen; i++) {
    if (s[i] != prefix[i]) {
      return 0;
    }
  }
  return 1;
}

static int ends_with_check_length(const uint32_t *s, int len, const uint32_t *suffix, int suffixLen) {
  if (len < suffixLen + 2) {
    return 0;
  }
  for (int i = 0; i < suffixLen; i++) {
    if (s[len - suffixLen + i] != suffix[i]) {
      return 0;
    }
  }
  return 1;
}

static int delete_n(uint32_t *s, int pos, int len, int n) {
  if (n <= 0 || pos < 0 || pos + n > len) {
    return len;
  }
  memmove(s + pos, s + pos + n, (size_t)(len - pos - n) * sizeof(uint32_t));
  return len - n;
}

static int stem_prefix(uint32_t *s, int len) {
  static const uint32_t p0[] = {0x0627, 0x0644};         /* ال */
  static const uint32_t p1[] = {0x0648, 0x0627, 0x0644}; /* وال */
  static const uint32_t p2[] = {0x0628, 0x0627, 0x0644}; /* بال */
  static const uint32_t p3[] = {0x0643, 0x0627, 0x0644}; /* كال */
  static const uint32_t p4[] = {0x0641, 0x0627, 0x0644}; /* فال */
  static const uint32_t p5[] = {0x0644, 0x0644};         /* لل */
  static const uint32_t p6[] = {0x0648};                 /* و */
  static const struct {
    const uint32_t *data;
    int len;
  } prefixes[] = {
      {p0, 2}, {p1, 3}, {p2, 3}, {p3, 3}, {p4, 3}, {p5, 2}, {p6, 1},
  };

  for (int i = 0; i < (int)(sizeof(prefixes) / sizeof(prefixes[0])); i++) {
    if (starts_with_check_length(s, len, prefixes[i].data, prefixes[i].len)) {
      return delete_n(s, 0, len, prefixes[i].len);
    }
  }
  return len;
}

static int stem_suffix(uint32_t *s, int len) {
  static const uint32_t s0[] = {0x0647, 0x0627}; /* ها */
  static const uint32_t s1[] = {0x0627, 0x0646}; /* ان */
  static const uint32_t s2[] = {0x0627, 0x062A}; /* ات */
  static const uint32_t s3[] = {0x0648, 0x0646}; /* ون */
  static const uint32_t s4[] = {0x064A, 0x0646}; /* ين */
  static const uint32_t s5[] = {0x064A, 0x0647}; /* يه */
  static const uint32_t s6[] = {0x064A, 0x0629}; /* ية */
  static const uint32_t s7[] = {0x0647};         /* ه */
  static const uint32_t s8[] = {0x0629};         /* ة */
  static const uint32_t s9[] = {0x064A};         /* ي */
  static const struct {
    const uint32_t *data;
    int len;
  } suffixes[] = {
      {s0, 2}, {s1, 2}, {s2, 2}, {s3, 2}, {s4, 2}, {s5, 2}, {s6, 2}, {s7, 1}, {s8, 1}, {s9, 1},
  };

  for (int i = 0; i < (int)(sizeof(suffixes) / sizeof(suffixes[0])); i++) {
    if (ends_with_check_length(s, len, suffixes[i].data, suffixes[i].len)) {
      len = delete_n(s, len - suffixes[i].len, len, suffixes[i].len);
    }
  }
  return len;
}

static int stem_token_arabic_light(const char *in, int nIn, char *out, int outCap) {
  uint32_t *codepoints = sqlite3_malloc((size_t)(nIn + 1) * sizeof(uint32_t));
  if (codepoints == NULL) {
    return -1;
  }

  int len = utf8_to_codepoints(in, nIn, codepoints, nIn + 1);
  len = stem_prefix(codepoints, len);
  len = stem_suffix(codepoints, len);

  int outLen = codepoints_to_utf8(codepoints, len, out, outCap);
  sqlite3_free(codepoints);
  return outLen;
}


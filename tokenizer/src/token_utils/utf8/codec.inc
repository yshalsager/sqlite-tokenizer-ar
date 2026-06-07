static int utf8_decode(const char *z, int n, int *nConsumed, uint32_t *codepoint) {
  if (n <= 0) {
    return 0;
  }
  const unsigned char c0 = (unsigned char)z[0];
  if (c0 < 0x80) {
    *codepoint = c0;
    *nConsumed = 1;
    return 1;
  }
  if ((c0 & 0xE0) == 0xC0 && n >= 2) {
    *codepoint = ((uint32_t)(c0 & 0x1F) << 6) | (uint32_t)(z[1] & 0x3F);
    *nConsumed = 2;
    return 1;
  }
  if ((c0 & 0xF0) == 0xE0 && n >= 3) {
    *codepoint = ((uint32_t)(c0 & 0x0F) << 12) | ((uint32_t)(z[1] & 0x3F) << 6) | (uint32_t)(z[2] & 0x3F);
    *nConsumed = 3;
    return 1;
  }
  if ((c0 & 0xF8) == 0xF0 && n >= 4) {
    *codepoint = ((uint32_t)(c0 & 0x07) << 18) | ((uint32_t)(z[1] & 0x3F) << 12) | ((uint32_t)(z[2] & 0x3F) << 6) |
                 (uint32_t)(z[3] & 0x3F);
    *nConsumed = 4;
    return 1;
  }
  return 0;
}

static int utf8_encode(uint32_t codepoint, char *out) {
  if (codepoint <= 0x7F) {
    out[0] = (char)codepoint;
    return 1;
  }
  if (codepoint <= 0x7FF) {
    out[0] = (char)(0xC0 | ((codepoint >> 6) & 0x1F));
    out[1] = (char)(0x80 | (codepoint & 0x3F));
    return 2;
  }
  if (codepoint <= 0xFFFF) {
    out[0] = (char)(0xE0 | ((codepoint >> 12) & 0x0F));
    out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    out[2] = (char)(0x80 | (codepoint & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | ((codepoint >> 18) & 0x07));
  out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
  out[3] = (char)(0x80 | (codepoint & 0x3F));
  return 4;
}


typedef uint32_t (*codepoint_transform)(uint32_t cp);

static int transform_token_utf8(const char *in, int nIn, char *out, int outCap, codepoint_transform transform) {
  int inPos = 0;
  int outPos = 0;
  while (inPos < nIn) {
    int consumed = 0;
    uint32_t cp = 0;
    if (!utf8_decode(in + inPos, nIn - inPos, &consumed, &cp)) {
      inPos++;
      continue;
    }
    inPos += consumed;

    cp = transform(cp);
    if (cp == 0) {
      continue;
    }

    char encoded[4];
    int encodedLen = utf8_encode(cp, encoded);
    if (encodedLen <= 0) {
      continue;
    }
    if (outPos + encodedLen > outCap) {
      break;
    }
    memcpy(out + outPos, encoded, encodedLen);
    outPos += encodedLen;
  }
  return outPos;
}

static int is_ascii_underscore_only(const char *token, int nToken) {
  if (nToken <= 0) {
    return 0;
  }
  for (int i = 0; i < nToken; i++) {
    if (token[i] != '_') {
      return 0;
    }
  }
  return 1;
}

static int contains_tatweel_codepoint(const char *token, int nToken) {
  int pos = 0;
  while (pos < nToken) {
    int consumed = 0;
    uint32_t cp = 0;
    if (!utf8_decode(token + pos, nToken - pos, &consumed, &cp)) {
      pos++;
      continue;
    }
    if (cp == 0x0640) {
      return 1;
    }
    pos += consumed;
  }
  return 0;
}


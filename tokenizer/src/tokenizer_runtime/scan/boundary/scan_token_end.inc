static int scan_token_end(const char *pText, int nText, int idx, int cls, int tokenIsNumeric) {
  uint32_t prevAcceptedCp = 0;
  while (idx < nText) {
    int cConsumed = 0;
    uint32_t cCp = 0;
    if (!utf8_decode(pText + idx, nText - idx, &cConsumed, &cCp)) {
      break;
    }
    if (cls == 1 && cCp == ':') {
      int lookConsumed = 0;
      uint32_t lookCp = 0;
      if (utf8_decode(pText + idx + cConsumed, nText - idx - cConsumed, &lookConsumed, &lookCp)) {
        int lookCls = token_char_class(lookCp);
        int prevIsDigit = is_digit_codepoint(prevAcceptedCp);
        int nextIsDigit = is_digit_codepoint(lookCp);
        if ((lookCls == 1 || lookCls == 2) && ((prevIsDigit && nextIsDigit) || (!prevIsDigit && !nextIsDigit))) {
          idx += cConsumed;
          prevAcceptedCp = cCp;
          continue;
        }
      }
    }
    if (cls == 2 && !tokenIsNumeric && cCp == ':') {
      int lookConsumed = 0;
      uint32_t lookCp = 0;
      if (utf8_decode(pText + idx + cConsumed, nText - idx - cConsumed, &lookConsumed, &lookCp) &&
          token_char_class(lookCp) != 0 && !is_digit_codepoint(lookCp) &&
          !(is_digit_codepoint(prevAcceptedCp) && token_char_class(lookCp) == 2)) {
        idx += cConsumed;
        prevAcceptedCp = cCp;
        continue;
      }
    }
    if (cls == 1 && cCp == '.') {
      int lookConsumed = 0;
      uint32_t lookCp = 0;
      if (utf8_decode(pText + idx + cConsumed, nText - idx - cConsumed, &lookConsumed, &lookCp)) {
        int lookCls = token_char_class(lookCp);
        if (lookCls != 0) {
          int prevIsDigit = is_digit_codepoint(prevAcceptedCp);
          int nextIsDigit = is_digit_codepoint(lookCp);
          if ((prevIsDigit && nextIsDigit) || (!prevIsDigit && !nextIsDigit)) {
            idx += cConsumed;
            prevAcceptedCp = cCp;
            continue;
          }
        }
      }
    }
    if (cls == 2 && !tokenIsNumeric && cCp == '.') {
      int lookConsumed = 0;
      uint32_t lookCp = 0;
      if (utf8_decode(pText + idx + cConsumed, nText - idx - cConsumed, &lookConsumed, &lookCp)) {
        int lookCls = token_char_class(lookCp);
        if (lookCls != 0) {
          int prevIsDigit = is_digit_codepoint(prevAcceptedCp);
          int nextIsDigit = is_digit_codepoint(lookCp);
          if ((prevIsDigit && nextIsDigit) || (!prevIsDigit && !nextIsDigit)) {
            idx += cConsumed;
            prevAcceptedCp = cCp;
            continue;
          }
        }
      }
    }
    if (cls == 1 && (cCp == '\'' || cCp == 0x2019)) {
      int lookConsumed = 0;
      uint32_t lookCp = 0;
      if (utf8_decode(pText + idx + cConsumed, nText - idx - cConsumed, &lookConsumed, &lookCp) &&
          token_char_class(lookCp) == 1) {
        idx += cConsumed;
        prevAcceptedCp = cCp;
        continue;
      }
    }
    if (cls == 2 && (cCp == '\'' || cCp == 0x2019)) {
      int lookConsumed = 0;
      uint32_t lookCp = 0;
      if (utf8_decode(pText + idx + cConsumed, nText - idx - cConsumed, &lookConsumed, &lookCp)) {
        int lookCls = token_char_class(lookCp);
        int prevIsDigit = is_digit_codepoint(prevAcceptedCp);
        int nextIsDigit = is_digit_codepoint(lookCp);
        if ((lookCls == 1 || lookCls == 2) && ((prevIsDigit && nextIsDigit) || (!prevIsDigit && !nextIsDigit))) {
          idx += cConsumed;
          prevAcceptedCp = cCp;
          continue;
        }
      }
    }
    if (cls == 1 && cCp == ';' && is_digit_codepoint(prevAcceptedCp)) {
      int lookConsumed = 0;
      uint32_t lookCp = 0;
      if (utf8_decode(pText + idx + cConsumed, nText - idx - cConsumed, &lookConsumed, &lookCp) &&
          is_digit_codepoint(lookCp)) {
        idx += cConsumed;
        prevAcceptedCp = cCp;
        continue;
      }
    }
    if (cls == 2 && cCp == ';' && is_digit_codepoint(prevAcceptedCp)) {
      int lookConsumed = 0;
      uint32_t lookCp = 0;
      if (utf8_decode(pText + idx + cConsumed, nText - idx - cConsumed, &lookConsumed, &lookCp) &&
          is_digit_codepoint(lookCp)) {
        idx += cConsumed;
        prevAcceptedCp = cCp;
        continue;
      }
    }
    if ((cls == 1 || cls == 2) && cCp == '_') {
      idx += cConsumed;
      prevAcceptedCp = cCp;
      continue;
    }
    if (cls == 1 && (cCp == ',' || cCp == 0x060C || cCp == 0x066B || cCp == 0x066C) && is_digit_codepoint(prevAcceptedCp)) {
      int lookConsumed = 0;
      uint32_t lookCp = 0;
      if (utf8_decode(pText + idx + cConsumed, nText - idx - cConsumed, &lookConsumed, &lookCp) && is_digit_codepoint(lookCp)) {
        idx += cConsumed;
        prevAcceptedCp = cCp;
        continue;
      }
    }
    if (cls == 2 && (cCp == ',' || cCp == 0x060C || cCp == 0x066B || cCp == 0x066C) && is_digit_codepoint(prevAcceptedCp)) {
      int lookConsumed = 0;
      uint32_t lookCp = 0;
      if (utf8_decode(pText + idx + cConsumed, nText - idx - cConsumed, &lookConsumed, &lookCp) && is_digit_codepoint(lookCp)) {
        idx += cConsumed;
        prevAcceptedCp = cCp;
        continue;
      }
    }
    if (tokenIsNumeric && (cCp == ',' || cCp == '.' || cCp == 0x060C || cCp == 0x066B || cCp == 0x066C)) {
      int lookConsumed = 0;
      uint32_t lookCp = 0;
      if (utf8_decode(pText + idx + cConsumed, nText - idx - cConsumed, &lookConsumed, &lookCp) &&
          is_digit_codepoint(lookCp)) {
        idx += cConsumed;
        prevAcceptedCp = cCp;
        continue;
      }
    }
    int nextCls = token_char_class(cCp);
    if (nextCls == 0) {
      break;
    }
    if (nextCls != cls) {
      if (cls == 1 && nextCls == 2 && (is_digit_codepoint(cCp) || prevAcceptedCp == '_')) {
        cls = 2;
      } else if (cls == 1 && nextCls == 2 && prevAcceptedCp == ':') {
        cls = 2;
      } else if (cls == 2 && nextCls == 1 && is_digit_codepoint(cCp)) {
        cls = 1;
      } else if (cls == 2 && nextCls == 1 && is_digit_codepoint(prevAcceptedCp)) {
        cls = 1;
      } else if (cls == 2 && tokenIsNumeric && nextCls == 1) {
        cls = 1;
        tokenIsNumeric = 0;
      } else if (cls == 2 && nextCls == 1 && prevAcceptedCp == ':') {
        cls = 1;
      } else if ((cls == 1 && nextCls == 3) || (cls == 3 && nextCls == 1)) {
        cls = 3;
      } else if (prevAcceptedCp == '.') {
        cls = nextCls;
      } else {
        break;
      }
    }
    idx += cConsumed;
    if (tokenIsNumeric && !is_digit_codepoint(cCp) && !is_numeric_connector_codepoint(cCp)) {
      tokenIsNumeric = 0;
    }
    prevAcceptedCp = cCp;
  }
  return idx;
}

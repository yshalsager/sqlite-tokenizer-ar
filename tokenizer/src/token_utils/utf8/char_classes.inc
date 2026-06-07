static int is_arabic_diacritic(uint32_t cp) {
  return cp == 0x064B || cp == 0x064C || cp == 0x064D || cp == 0x064E || cp == 0x064F || cp == 0x0650 || cp == 0x0651 ||
         cp == 0x0652;
}

static int is_combining_mark(uint32_t cp) {
  return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x1DC0 && cp <= 0x1DFF) ||
         (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE20 && cp <= 0xFE2F);
}

static uint32_t fold_codepoint_stage1(uint32_t cp) {
  if (cp >= 'A' && cp <= 'Z') {
    return cp + 32;
  }

  if (cp >= 0x0660 && cp <= 0x0669) {
    return (uint32_t)('0' + (cp - 0x0660));
  }
  if (cp >= 0x06F0 && cp <= 0x06F9) {
    return (uint32_t)('0' + (cp - 0x06F0));
  }
  return cp;
}

static uint32_t normalize_codepoint_stage2(uint32_t cp) {
  if (cp == 0x0622 || cp == 0x0623 || cp == 0x0625) {
    return 0x0627;
  }
  if (cp == 0x0649) {
    return 0x064A;
  }
  if (cp == 0x0629) {
    return 0x0647;
  }
  if (cp == 0x0640 || is_arabic_diacritic(cp)) {
    return 0;
  }

  return cp;
}

static int token_char_class(uint32_t cp) {
  /* 0: not token, 1: ASCII alnum, 2: Arabic token char, 3: other Unicode token char */
  if (cp <= 0x20 || cp == 0x00A0 || cp == 0x1680 || cp == 0x2028 || cp == 0x2029 || cp == 0x202F || cp == 0x205F ||
      cp == 0x3000) {
    return 0;
  }
  if (cp == '_' || (cp >= '0' && cp <= '9') || (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z')) {
    return 1;
  }
  if ((cp >= 0x0660 && cp <= 0x0669) || (cp >= 0x06F0 && cp <= 0x06F9)) {
    return 2;
  }
  if ((cp >= 0x0621 && cp <= 0x063A) || (cp >= 0x0641 && cp <= 0x064A) || (cp >= 0x066E && cp <= 0x066F) ||
      (cp >= 0x0671 && cp <= 0x06D3) || cp == 0x06D5 || (cp >= 0x06EE && cp <= 0x06EF) || (cp >= 0x06FA && cp <= 0x06FC) ||
      cp == 0x06FF) {
    return 2;
  }
  if (cp == 0x0640 || cp == 0x0670 || (cp >= 0x064B && cp <= 0x065F) || (cp >= 0x06D6 && cp <= 0x06ED) || cp == 0x200D) {
    return 2;
  }
  if (cp == 0x060C || cp == 0x061B || cp == 0x061F || cp == 0x066A || cp == 0x066B || cp == 0x066C || cp == 0x066D ||
      cp == 0x06D4) {
    return 0;
  }
  if ((cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x20A0 && cp <= 0x20CF) || (cp >= 0x2E00 && cp <= 0x2E7F) ||
      (cp >= 0x3000 && cp <= 0x303F) ||
      (cp >= 0xFE10 && cp <= 0xFE1F) || (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF65)) {
    return 0;
  }
  if (cp == 0xFDFA || cp == 0xFDFB) {
    return 2;
  }
  if ((cp >= 0xFB50 && cp <= 0xFDFF) || (cp >= 0xFE70 && cp <= 0xFEFF)) {
    return 0;
  }
  if (cp == 0x00A6 || cp == 0x00AB || cp == 0x00AC || cp == 0x00AD || cp == 0x00B7 || cp == 0x00BB || cp == 0x00D7 ||
      cp == 0x00F7 || cp == 0x2018 || cp == 0x2019 ||
      cp == 0x201C || cp == 0x201D || cp == 0x2026 || cp == 0x2997 || cp == 0x2998) {
    return 0;
  }
  if (cp >= 0x80) {
    return 3;
  }
  return 0;
}

static int is_digit_codepoint(uint32_t cp) {
  return (cp >= '0' && cp <= '9') || (cp >= 0x0660 && cp <= 0x0669) || (cp >= 0x06F0 && cp <= 0x06F9);
}

static int is_numeric_connector_codepoint(uint32_t cp) {
  return cp == '.' || cp == ',' || cp == ';' || cp == 0x060C || cp == 0x066B || cp == 0x066C;
}


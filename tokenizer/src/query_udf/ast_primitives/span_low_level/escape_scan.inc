static int is_escaped_at(const char *text, int start, int index) {
  int count = 0;
  int cursor = index - 1;
  while (cursor >= start && text[cursor] == '\\') {
    count++;
    cursor--;
  }
  return (count % 2) == 1;
}

static int find_unescaped_char_span(const char *text, int start, int end, char target) {
  for (int idx = start; idx < end; idx++) {
    if (text[idx] != target) {
      continue;
    }
    if (!is_escaped_at(text, start, idx)) {
      return idx;
    }
  }
  return -1;
}

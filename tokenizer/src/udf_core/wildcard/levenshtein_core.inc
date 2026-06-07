static int levenshtein_codepoints(const uint32_t *a, int nA, const uint32_t *b, int nB, int maxEdits) {
  if (maxEdits < 0) {
    maxEdits = 0;
  }
  if (nA == nB && nA > 0 && memcmp(a, b, (size_t)nA * sizeof(uint32_t)) == 0) {
    return 0;
  }
  if (nA == 0) {
    return nB;
  }
  if (nB == 0) {
    return nA;
  }
  int lenDiff = nA - nB;
  if (lenDiff < 0) {
    lenDiff = -lenDiff;
  }
  if (lenDiff > maxEdits) {
    return maxEdits + 1;
  }

  int *prev = sqlite3_malloc((size_t)(nB + 1) * sizeof(int));
  int *curr = sqlite3_malloc((size_t)(nB + 1) * sizeof(int));
  if (prev == NULL || curr == NULL) {
    sqlite3_free(prev);
    sqlite3_free(curr);
    return -1;
  }

  for (int j = 0; j <= nB; j++) {
    prev[j] = j;
  }

  for (int i = 1; i <= nA; i++) {
    curr[0] = i;
    int minInRow = curr[0];
    for (int j = 1; j <= nB; j++) {
      int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      int deletion = prev[j] + 1;
      int insertion = curr[j - 1] + 1;
      int substitution = prev[j - 1] + cost;
      int best = deletion;
      if (insertion < best) {
        best = insertion;
      }
      if (substitution < best) {
        best = substitution;
      }
      curr[j] = best;
      if (best < minInRow) {
        minInRow = best;
      }
    }
    if (minInRow > maxEdits) {
      sqlite3_free(prev);
      sqlite3_free(curr);
      return maxEdits + 1;
    }
    int *tmp = prev;
    prev = curr;
    curr = tmp;
  }

  int distance = prev[nB];
  sqlite3_free(prev);
  sqlite3_free(curr);
  return distance;
}


static int levenshtein_over_limit(int maxEdits) {
  return maxEdits == INT_MAX ? INT_MAX : maxEdits + 1;
}

static int levenshtein_codepoints(const uint32_t *a, int nA, const uint32_t *b, int nB, int maxEdits) {
  if (maxEdits < 0) {
    maxEdits = 0;
  }
  int lenDiff = nA - nB;
  if (lenDiff < 0) {
    lenDiff = -lenDiff;
  }
  if (lenDiff > maxEdits) {
    return levenshtein_over_limit(maxEdits);
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

  int cols = 0;
  if (ar_checked_int_sum(nB, 1, &cols) != SQLITE_OK) {
    return -1;
  }
  int *prev = ar_malloc_array(cols, sizeof(int));
  int *curr = ar_malloc_array(cols, sizeof(int));
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
      return levenshtein_over_limit(maxEdits);
    }
    int *tmp = prev;
    prev = curr;
    curr = tmp;
  }

  int distance = prev[nB];
  sqlite3_free(prev);
  sqlite3_free(curr);
  if (distance > maxEdits) {
    return levenshtein_over_limit(maxEdits);
  }
  return distance;
}

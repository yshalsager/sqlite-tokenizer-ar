static int ar_checked_int_sum(sqlite3_int64 left, sqlite3_int64 right, int *out) {
  if (left < 0 || right < 0 || left > INT_MAX || right > INT_MAX || left > (sqlite3_int64)INT_MAX - right) {
    return SQLITE_TOOBIG;
  }
  *out = (int)(left + right);
  return SQLITE_OK;
}

static int ar_utf8_expanded_capacity(int nInput, int *outCap) {
  if (nInput < 0) {
    return SQLITE_TOOBIG;
  }
  sqlite3_int64 cap = (nInput <= 0) ? 4 : ((sqlite3_int64)nInput * 4 + 4);
  if (cap > INT_MAX) {
    return SQLITE_TOOBIG;
  }
  *outCap = (int)cap;
  return SQLITE_OK;
}

static int ar_grow_capacity(int currentCap, int required, int minCap, int *outCap) {
  if (required < 0 || minCap <= 0) {
    return SQLITE_TOOBIG;
  }
  sqlite3_int64 cap = currentCap <= 0 ? minCap : currentCap;
  while (cap < required) {
    if (cap > INT_MAX / 2) {
      return SQLITE_TOOBIG;
    }
    cap *= 2;
  }
  *outCap = (int)cap;
  return SQLITE_OK;
}

static int ar_array_bytes(int count, size_t elemSize, sqlite3_uint64 *outBytes) {
  if (count < 0 || elemSize == 0) {
    return SQLITE_TOOBIG;
  }
  sqlite3_uint64 bytes = (sqlite3_uint64)count * (sqlite3_uint64)elemSize;
  if (count != 0 && bytes / (sqlite3_uint64)count != (sqlite3_uint64)elemSize) {
    return SQLITE_TOOBIG;
  }
  *outBytes = bytes;
  return SQLITE_OK;
}

static void *ar_malloc_array(int count, size_t elemSize) {
  sqlite3_uint64 bytes = 0;
  if (ar_array_bytes(count, elemSize, &bytes) != SQLITE_OK) {
    return NULL;
  }
  return sqlite3_malloc64(bytes);
}

static void *ar_realloc_array(void *ptr, int count, size_t elemSize) {
  sqlite3_uint64 bytes = 0;
  if (ar_array_bytes(count, elemSize, &bytes) != SQLITE_OK) {
    return NULL;
  }
  return sqlite3_realloc64(ptr, bytes);
}

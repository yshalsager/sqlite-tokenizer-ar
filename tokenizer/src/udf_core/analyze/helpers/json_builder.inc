static int json_builder_ensure(char **zOut, int *nOut, int *nCap, int nAppend) {
  if (nAppend < 0) {
    return SQLITE_TOOBIG;
  }
  int requiredNoTerm = 0;
  if (ar_checked_int_sum(*nOut, nAppend, &requiredNoTerm) != SQLITE_OK) {
    return SQLITE_TOOBIG;
  }
  int target = 0;
  if (ar_checked_int_sum(requiredNoTerm, 1, &target) != SQLITE_OK) {
    return SQLITE_TOOBIG;
  }
  if (target <= *nCap) {
    return SQLITE_OK;
  }
  int cap = 0;
  int rc = ar_grow_capacity(*nCap, target, 64, &cap);
  if (rc != SQLITE_OK) {
    return rc;
  }
  char *next = sqlite3_realloc64(*zOut, (sqlite3_uint64)cap);
  if (next == NULL) {
    return SQLITE_NOMEM;
  }
  *zOut = next;
  *nCap = cap;
  return SQLITE_OK;
}

static int json_builder_append_bytes(char **zOut, int *nOut, int *nCap, const char *z, int n) {
  int rc = json_builder_ensure(zOut, nOut, nCap, n);
  if (rc != SQLITE_OK) {
    return rc;
  }
  memcpy(*zOut + *nOut, z, (size_t)n);
  *nOut += n;
  return SQLITE_OK;
}

static int json_builder_append_char(char **zOut, int *nOut, int *nCap, char c) {
  return json_builder_append_bytes(zOut, nOut, nCap, &c, 1);
}

static int json_builder_append_escaped_string(char **zOut, int *nOut, int *nCap, const char *z, int n) {
  int rc = json_builder_append_char(zOut, nOut, nCap, '"');
  if (rc != SQLITE_OK) {
    return rc;
  }
  for (int i = 0; i < n; i++) {
    unsigned char c = (unsigned char)z[i];
    if (c == '"' || c == '\\') {
      char escaped[2] = {'\\', (char)c};
      rc = json_builder_append_bytes(zOut, nOut, nCap, escaped, 2);
    } else if (c == '\b') {
      rc = json_builder_append_bytes(zOut, nOut, nCap, "\\b", 2);
    } else if (c == '\f') {
      rc = json_builder_append_bytes(zOut, nOut, nCap, "\\f", 2);
    } else if (c == '\n') {
      rc = json_builder_append_bytes(zOut, nOut, nCap, "\\n", 2);
    } else if (c == '\r') {
      rc = json_builder_append_bytes(zOut, nOut, nCap, "\\r", 2);
    } else if (c == '\t') {
      rc = json_builder_append_bytes(zOut, nOut, nCap, "\\t", 2);
    } else if (c < 0x20) {
      char hex[7];
      snprintf(hex, sizeof(hex), "\\u%04X", (unsigned int)c);
      rc = json_builder_append_bytes(zOut, nOut, nCap, hex, 6);
    } else {
      rc = json_builder_append_char(zOut, nOut, nCap, (char)c);
    }
    if (rc != SQLITE_OK) {
      return rc;
    }
  }
  return json_builder_append_char(zOut, nOut, nCap, '"');
}

static int json_builder_append_int(char **zOut, int *nOut, int *nCap, int value) {
  char buf[32];
  int n = snprintf(buf, sizeof(buf), "%d", value);
  if (n <= 0) {
    return SQLITE_ERROR;
  }
  return json_builder_append_bytes(zOut, nOut, nCap, buf, n);
}

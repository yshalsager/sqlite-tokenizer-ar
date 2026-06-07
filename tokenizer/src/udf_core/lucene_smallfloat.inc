static int lucene_long_to_int4(int value) {
  if (value < 0) {
    return 0;
  }
  unsigned int v = (unsigned int)value;
  int numBits = 0;
  while (v > 0U) {
    numBits++;
    v >>= 1U;
  }
  if (numBits < 4) {
    return value;
  }
  int shift = numBits - 4;
  int encoded = (value >> shift) & 0x07;
  encoded |= (shift + 1) << 3;
  return encoded;
}

static int lucene_int4_to_long(int value) {
  int bits = value & 0x07;
  int shift = (value >> 3) - 1;
  if (shift == -1) {
    return bits;
  }
  return (bits | 0x08) << shift;
}

static int lucene_int_to_byte4(int value) {
  if (value < 0) {
    return -1;
  }
  if (value < LUCENE_NUM_FREE_VALUES) {
    return value;
  }
  return LUCENE_NUM_FREE_VALUES + lucene_long_to_int4(value - LUCENE_NUM_FREE_VALUES);
}

static int lucene_byte4_to_int(int value) {
  if (value < LUCENE_NUM_FREE_VALUES) {
    return value;
  }
  return LUCENE_NUM_FREE_VALUES + lucene_int4_to_long(value - LUCENE_NUM_FREE_VALUES);
}

static void udf_sqlite_tokenizer_ar_int_to_byte4(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_int_to_byte4 expects 1 argument", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  int value = sqlite3_value_int(argv[0]);
  int encoded = lucene_int_to_byte4(value);
  if (encoded < 0) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_int_to_byte4 expects non-negative integer input", -1);
    return;
  }
  sqlite3_result_int(context, encoded);
}

static void udf_sqlite_tokenizer_ar_byte4_to_int(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 1) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_byte4_to_int expects 1 argument", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  int value = sqlite3_value_int(argv[0]);
  if (value < 0 || value > 255) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_byte4_to_int expects integer in range 0..255", -1);
    return;
  }
  sqlite3_result_int(context, lucene_byte4_to_int(value));
}

static void udf_sqlite_tokenizer_ar_norm_inverse_cache_blob(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 3) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_norm_inverse_cache_blob expects 3 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL ||
      sqlite3_value_type(argv[2]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }

  float k1 = (float)sqlite3_value_double(argv[0]);
  float b = (float)sqlite3_value_double(argv[1]);
  float avgdl = (float)sqlite3_value_double(argv[2]);

  float *cache = sqlite3_malloc((size_t)256 * sizeof(float));
  if (cache == NULL) {
    sqlite3_result_error_nomem(context);
    return;
  }

  if (avgdl <= 0.0f) {
    for (int idx = 0; idx < 256; idx++) {
      cache[idx] = 0.0f;
    }
  } else {
    float oneMinusB = (float)(1.0f - b);
    for (int idx = 0; idx < 256; idx++) {
      float docLen = (float)lucene_byte4_to_int(idx);
      float scaled = (float)((float)(b * docLen) / avgdl);
      float norm = (float)(oneMinusB + scaled);
      float denom = (float)(k1 * norm);
      cache[idx] = (denom <= 0.0f) ? 0.0f : (float)(1.0f / denom);
    }
  }

  sqlite3_result_blob(context, cache, (int)(256 * sizeof(float)), sqlite3_free);
}

static void udf_sqlite_tokenizer_ar_f32_mul(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 2) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_f32_mul expects 2 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  float left = (float)sqlite3_value_double(argv[0]);
  float right = (float)sqlite3_value_double(argv[1]);
  float out = (float)(left * right);
  sqlite3_result_double(context, (double)out);
}

static void udf_sqlite_tokenizer_ar_f32_add(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 2) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_f32_add expects 2 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  float left = (float)sqlite3_value_double(argv[0]);
  float right = (float)sqlite3_value_double(argv[1]);
  float out = (float)(left + right);
  sqlite3_result_double(context, (double)out);
}

static void udf_sqlite_tokenizer_ar_lucene_idf(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 2) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_lucene_idf expects 2 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL) {
    sqlite3_result_null(context);
    return;
  }
  float docCount = (float)sqlite3_value_double(argv[0]);
  float docFreq = (float)sqlite3_value_double(argv[1]);
  if (docCount <= 0.0f || docFreq <= 0.0f) {
    sqlite3_result_double(context, 0.0);
    return;
  }
  float idf = (float)log(1.0 + ((docCount - docFreq + 0.5) / (docFreq + 0.5)));
  sqlite3_result_double(context, (double)idf);
}

static void udf_sqlite_tokenizer_ar_lucene_term_score(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 6) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_lucene_term_score expects 6 arguments", -1);
    return;
  }
  for (int i = 0; i < 6; i++) {
    if (sqlite3_value_type(argv[i]) == SQLITE_NULL) {
      sqlite3_result_null(context);
      return;
    }
  }

  float weight = (float)sqlite3_value_double(argv[0]);
  float tf = (float)sqlite3_value_double(argv[1]);
  int rawDocLen = sqlite3_value_int(argv[2]);
  if (rawDocLen < 0) {
    rawDocLen = 0;
  }
  float k1 = (float)sqlite3_value_double(argv[3]);
  float b = (float)sqlite3_value_double(argv[4]);
  float avgdl = (float)sqlite3_value_double(argv[5]);

  if (k1 <= 0.0f || avgdl <= 0.0f || tf <= 0.0f) {
    sqlite3_result_double(context, 0.0);
    return;
  }

  int normByte = lucene_int_to_byte4(rawDocLen);
  if (normByte < 0) {
    sqlite3_result_double(context, 0.0);
    return;
  }
  float docLen = (float)lucene_byte4_to_int(normByte);
  float oneMinusB = (float)(1.0f - b);
  float scaled = (float)((float)(b * docLen) / avgdl);
  float norm = (float)(oneMinusB + scaled);
  float denom = (float)(k1 * norm);
  if (denom <= 0.0f) {
    sqlite3_result_double(context, 0.0);
    return;
  }
  float normInverse = (float)(1.0f / denom);
  float inner = (float)(1.0f + (float)(tf * normInverse));
  if (inner <= 0.0f) {
    sqlite3_result_double(context, 0.0);
    return;
  }
  float score = (float)(weight - (float)(weight / inner));
  sqlite3_result_double(context, (double)score);
}

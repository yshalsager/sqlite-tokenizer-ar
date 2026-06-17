#ifndef SQLITE_DIRECTONLY
#define SQLITE_DIRECTONLY 0
#endif

typedef struct UdfDef {
  const char *name;
  int argc;
  int flags;
  void (*xFunc)(sqlite3_context *, int, sqlite3_value **);
} UdfDef;

#define AR_UDF_DETERMINISTIC SQLITE_DETERMINISTIC
#define AR_UDF_DB_READING SQLITE_DIRECTONLY

#include "init/udf_list_core.inc"
#include "init/udf_list_query.inc"

static int register_udf_list(sqlite3 *db, const UdfDef *defs, int nDefs) {
  for (int i = 0; i < nDefs; i++) {
    int rc = sqlite3_create_function(
        db,
        defs[i].name,
        defs[i].argc,
        SQLITE_UTF8 | defs[i].flags,
        NULL,
        defs[i].xFunc,
        NULL,
        NULL);
    if (rc != SQLITE_OK) {
      return rc;
    }
  }
  return SQLITE_OK;
}

static fts5_api *fts5_api_from_db(sqlite3 *db) {
  sqlite3_stmt *stmt = NULL;
  fts5_api *fts = NULL;
  int rc = sqlite3_prepare_v2(db, "SELECT fts5(?1)", -1, &stmt, NULL);
  if (rc != SQLITE_OK) {
    return NULL;
  }
  sqlite3_bind_pointer(stmt, 1, (void *)&fts, "fts5_api_ptr", NULL);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  return fts;
}

static void sqlite_tokenizer_ar_set_error(char **pzErrMsg, const char *message) {
  if (pzErrMsg != NULL) {
    *pzErrMsg = sqlite3_mprintf("%s", message);
  }
}

int sqlite3_sqlitetokenizerar_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
  SQLITE_EXTENSION_INIT2(pApi);

  fts5_api *fts = fts5_api_from_db(db);
  if (fts == NULL) {
    sqlite_tokenizer_ar_set_error(pzErrMsg, "sqlite-tokenizer-ar requires SQLite FTS5");
    return SQLITE_ERROR;
  }

  static fts5_tokenizer tokenizer = {
      .xCreate = arabic_create,
      .xDelete = arabic_delete,
      .xTokenize = arabic_tokenize,
  };

  int rc = fts->xCreateTokenizer(fts, "sqlite_tokenizer_ar", NULL, &tokenizer, NULL);
  if (rc != SQLITE_OK) {
    sqlite_tokenizer_ar_set_error(pzErrMsg, "failed to register sqlite_tokenizer_ar FTS5 tokenizer");
    return rc;
  }

  rc = register_udf_list(db, kCoreUdfs, (int)(sizeof(kCoreUdfs) / sizeof(kCoreUdfs[0])));
  if (rc != SQLITE_OK) {
    sqlite_tokenizer_ar_set_error(pzErrMsg, "failed to register sqlite-tokenizer-ar core UDFs");
    return rc;
  }
  rc = register_udf_list(db, kQueryUdfs, (int)(sizeof(kQueryUdfs) / sizeof(kQueryUdfs[0])));
  if (rc != SQLITE_OK) {
    sqlite_tokenizer_ar_set_error(pzErrMsg, "failed to register sqlite-tokenizer-ar query UDFs");
  }
  return rc;
}

int sqlite3_extension_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi) {
  return sqlite3_sqlitetokenizerar_init(db, pzErrMsg, pApi);
}

#ifdef SQLITE_CORE
int sqlite_tokenizer_ar_register(sqlite3 *db) {
  return sqlite3_sqlitetokenizerar_init(db, NULL, NULL);
}
#endif

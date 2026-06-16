#ifndef SQLITE_TOKENIZER_AR_H
#define SQLITE_TOKENIZER_AR_H

#include <sqlite3.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sqlite3_api_routines sqlite3_api_routines;

int sqlite3_sqlitetokenizerar_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi);
int sqlite3_extension_init(sqlite3 *db, char **pzErrMsg, const sqlite3_api_routines *pApi);
int sqlite_tokenizer_ar_register(sqlite3 *db);

#ifdef __cplusplus
}
#endif

#endif

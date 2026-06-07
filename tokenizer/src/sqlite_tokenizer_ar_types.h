#ifndef SQLITE_TOKENIZER_AR_TYPES_H
#define SQLITE_TOKENIZER_AR_TYPES_H

/* Internal placeholder for analyzer-empty tokens (e.g., tatweel-only terms). */
#define EMPTY_TERM_SENTINEL "\xEE\x80\x80" /* U+E000 */
#define EMPTY_TERM_SENTINEL_LEN 3
#define STANDARD_TOKEN_MAX_CODEPOINTS 255
#define LUCENE_MAX_INT4 231
#define LUCENE_NUM_FREE_VALUES (255 - LUCENE_MAX_INT4)

typedef struct StemExclusionTerm {
  char *term;
  int len;
} StemExclusionTerm;

typedef struct StopwordTerm {
  char *term;
  int len;
} StopwordTerm;

typedef struct ArabicTokenizer {
  StemExclusionTerm *stemExclusions;
  int stemExclusionCount;
  StopwordTerm *stopwords;
  int stopwordCount;
  int hasCustomStopwords;
} ArabicTokenizer;

typedef struct AnalyzeTermsBuffer {
  char **terms;
  int *termLens;
  int *positions;
  int count;
  int cap;
  int oom;
} AnalyzeTermsBuffer;

typedef struct NormalizeOptions {
  int ignoreDiacritics;
  int ignoreHamzaForms;
  int ignoreLetterForms;
  int ignoreDigitForms;
  int lowercase;
  int keepWildcards;
} NormalizeOptions;

#ifndef FTS5_TOKENIZE_QUERY
#define FTS5_TOKENIZE_QUERY 0x0001
#endif

/* Internal tokenizer mode used by helper UDFs to return logical positions. */
#define ARABIC_TOKENIZE_EMIT_POSITIONS 0x10000

static int arabic_tokenize(
    Fts5Tokenizer *pTokenizer,
    void *pCtx,
    int flags,
    const char *pText,
    int nText,
    int (*xToken)(void *pCtx, int tflags, const char *pToken, int nToken, int iStart, int iEnd));

#endif

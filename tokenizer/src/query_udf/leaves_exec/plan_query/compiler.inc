static int plan_query_normalize_pattern(
    PlanQueryContext *plan, const char *text, int nText, char **normalizedOut, int *nNormalizedOut) {
  NormalizeOptions options = {
      .ignoreDiacritics = plan->options.ignoreDiacritics,
      .ignoreHamzaForms = plan->options.ignoreHamzaForms,
      .ignoreLetterForms = plan->options.ignoreLetterForms,
      .ignoreDigitForms = plan->options.ignoreDigitForms,
      .lowercase = 1,
      .keepWildcards = 1,
  };
  return normalize_text_with_options(text, nText, &options, normalizedOut, nNormalizedOut);
}

static int plan_query_append_expansion(
    PlanQueryContext *plan,
    const char *kind,
    const char *pattern,
    int nPattern,
    int maxExpansions,
    int edits,
    char **compiledOut,
    int *nCompiledOut) {
  plan->expansionId++;
  char id[64];
  int nId = sqlite3_snprintf((int)sizeof(id), id, "%.*s_%d", plan->nField, plan->field, plan->expansionId)
                ? (int)strlen(id)
                : 0;
  if (nId <= 0 || nId >= (int)sizeof(id)) {
    return SQLITE_NOMEM;
  }
  int rc = SQLITE_OK;
  if (plan->expandCount > 0) {
    rc = json_builder_append_char(&plan->expandJson, &plan->nExpandJson, &plan->expandJsonCap, ',');
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&plan->expandJson, &plan->nExpandJson, &plan->expandJsonCap, "{\"id\":", 6);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(&plan->expandJson, &plan->nExpandJson, &plan->expandJsonCap, id, nId);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&plan->expandJson, &plan->nExpandJson, &plan->expandJsonCap, ",\"field\":", 9);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(&plan->expandJson, &plan->nExpandJson, &plan->expandJsonCap, plan->field, plan->nField);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&plan->expandJson, &plan->nExpandJson, &plan->expandJsonCap, ",\"kind\":", 8);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(&plan->expandJson, &plan->nExpandJson, &plan->expandJsonCap, kind, (int)strlen(kind));
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&plan->expandJson, &plan->nExpandJson, &plan->expandJsonCap, ",\"pattern\":", 11);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_escaped_string(&plan->expandJson, &plan->nExpandJson, &plan->expandJsonCap, pattern, nPattern);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&plan->expandJson, &plan->nExpandJson, &plan->expandJsonCap, ",\"max_expansions\":", 18);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_int(&plan->expandJson, &plan->nExpandJson, &plan->expandJsonCap, maxExpansions);
  }
  if (rc == SQLITE_OK && edits >= 0) {
    rc = json_builder_append_bytes(&plan->expandJson, &plan->nExpandJson, &plan->expandJsonCap, ",\"edits\":", 9);
    if (rc == SQLITE_OK) {
      rc = json_builder_append_int(&plan->expandJson, &plan->nExpandJson, &plan->expandJsonCap, edits);
    }
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_char(&plan->expandJson, &plan->nExpandJson, &plan->expandJsonCap, '}');
  }
  if (rc != SQLITE_OK) {
    return rc;
  }
  plan->expandCount++;

  int nCompiled = nId + 1;
  char *compiled = sqlite3_malloc64((sqlite3_uint64)nCompiled + 1);
  if (compiled == NULL) {
    return SQLITE_NOMEM;
  }
  compiled[0] = '$';
  memcpy(compiled + 1, id, (size_t)nId);
  compiled[nCompiled] = '\0';
  *compiledOut = compiled;
  *nCompiledOut = nCompiled;
  return SQLITE_OK;
}

static int plan_query_analyze_count(ArabicTokenizer *tokenizer, const char *text, int nText, int *countOut) {
  AnalyzeTermsBuffer terms;
  memset(&terms, 0, sizeof(terms));
  int rc = arabic_tokenize((Fts5Tokenizer *)tokenizer, &terms, FTS5_TOKENIZE_QUERY, text, nText, analyze_collect_token);
  if (rc == SQLITE_OK) {
    rc = terms.rc;
  }
  *countOut = terms.count;
  analyze_terms_buffer_free(&terms);
  return rc;
}

static int plan_query_compile_phrase(
    PlanQueryContext *plan,
    const ParsedSimpleAstClause *clause,
    int runtimeFieldKind,
    char **compiledOut,
    int *nCompiledOut) {
  ExecutePhraseClauseView view;
  if (!execute_query_decode_phrase_clause(clause, &view)) {
    return SQLITE_NOTFOUND;
  }
  if (view.scopeFieldKind != 0 && view.scopeFieldKind != runtimeFieldKind) {
    return execute_query_build_unwrapped_single_term_expression("__nohit__", 9, compiledOut, nCompiledOut);
  }
  ExecutePhraseTerm *terms = NULL;
  int termCount = 0;
  int rc = execute_query_collect_phrase_terms_with_tokenizer(plan->tokenizer, view.text, view.nText, &terms, &termCount);
  if (rc == SQLITE_OK) {
    rc = execute_query_build_phrase_compiled(terms, termCount, view.slop, compiledOut, nCompiledOut);
  }
  execute_query_free_phrase_terms(terms, termCount);
  if (rc == SQLITE_OK) {
    rc = execute_query_unwrap_outer_parens(compiledOut, nCompiledOut);
  }
  return rc;
}

static int plan_query_compile_token(
    PlanQueryContext *plan,
    const ParsedSimpleAstClause *clause,
    int runtimeFieldKind,
    char **compiledOut,
    int *nCompiledOut) {
  const char *scope = NULL;
  int nScope = 0;
  const char *raw = NULL;
  int nRaw = 0;
  parse_boosted_token_clause_scope(clause->value, clause->nValue, &scope, &nScope, &raw, &nRaw);
  if (scope != NULL && execute_query_field_kind(scope, nScope) != runtimeFieldKind) {
    return execute_query_build_unwrapped_single_term_expression("__nohit__", 9, compiledOut, nCompiledOut);
  }
  const char *base = raw;
  int nBase = nRaw;
  char boost[64];
  int nBoost = 0;
  double boostValue = 1.0;
  int stripRc = parse_boosted_token_clause_strip_boost(
      raw, nRaw, &base, &nBase, boost, (int)sizeof(boost), &nBoost, &boostValue);
  if (stripRc < 0 || nBase <= 0) {
    return SQLITE_ERROR;
  }

  const char *fuzzyBase = NULL;
  int nFuzzyBase = 0;
  int edits = 0;
  if (parse_fuzzy_token_span(base, nBase, &fuzzyBase, &nFuzzyBase, &edits)) {
    if (!plan->options.allowFuzzy) {
      return SQLITE_ERROR;
    }
    char *normalized = NULL;
    int nNormalized = 0;
    int rc = plan_query_normalize_pattern(plan, fuzzyBase, nFuzzyBase, &normalized, &nNormalized);
    if (rc == SQLITE_OK) {
      rc = plan_query_append_expansion(
          plan, "fuzzy", normalized, nNormalized, plan->options.fuzzyMax, edits, compiledOut, nCompiledOut);
    }
    sqlite3_free(normalized);
    return rc;
  }

  if (parse_boosted_token_clause_has_unescaped_wildcard(base, nBase)) {
    if (!plan->options.allowWildcard) {
      return SQLITE_ERROR;
    }
    char *normalized = NULL;
    int nNormalized = 0;
    int rc = plan_query_normalize_pattern(plan, base, nBase, &normalized, &nNormalized);
    if (rc != SQLITE_OK) {
      sqlite3_free(normalized);
      return rc;
    }
    int starCount = 0;
    int questionCount = 0;
    int literalCount = 0;
    for (int i = 0; i < nNormalized; i++) {
      if (normalized[i] == '\\' && i + 1 < nNormalized) {
        literalCount++;
        i++;
      } else if (normalized[i] == '*') {
        starCount++;
      } else if (normalized[i] == '?') {
        questionCount++;
      } else if (!isspace((unsigned char)normalized[i])) {
        literalCount++;
      }
    }
    if (literalCount == 0 || find_unescaped_char_span(normalized, 0, nNormalized, '[') >= 0 ||
        find_unescaped_char_span(normalized, 0, nNormalized, ']') >= 0) {
      sqlite3_free(normalized);
      return SQLITE_ERROR;
    }
    int hasLeadingStar = normalized[0] == '*';
    int hasTrailingStar = normalized[nNormalized - 1] == '*' && !is_escaped_at(normalized, 0, nNormalized - 1);
    if ((hasLeadingStar && !plan->options.allowSuffix) || (hasTrailingStar && !plan->options.allowPrefix)) {
      sqlite3_free(normalized);
      return SQLITE_ERROR;
    }
    int simplePrefix = nNormalized > 1 && normalized[nNormalized - 1] == '*' && normalized[0] != '*' && starCount == 1 && questionCount == 0;
    if (simplePrefix) {
      char *compiled = NULL;
      int nCompiled = 0;
      int compiledCap = 0;
      rc = execute_query_append_match_term(&compiled, &nCompiled, &compiledCap, normalized, nNormalized - 1);
      if (rc == SQLITE_OK) {
        rc = json_builder_append_char(&compiled, &nCompiled, &compiledCap, '*');
      }
      sqlite3_free(normalized);
      if (rc != SQLITE_OK) {
        sqlite3_free(compiled);
        return rc;
      }
      compiled[nCompiled] = '\0';
      *compiledOut = compiled;
      *nCompiledOut = nCompiled;
      return SQLITE_OK;
    }
    const char *kind = "wildcard";
    int maxExpansions = plan->options.wildcardMax;
    if (nNormalized > 1 && normalized[0] == '*' && normalized[nNormalized - 1] != '*' && starCount == 1 && questionCount == 0) {
      kind = "suffix";
      maxExpansions = plan->options.suffixMax;
    }
    rc = plan_query_append_expansion(plan, kind, normalized, nNormalized, maxExpansions, -1, compiledOut, nCompiledOut);
    sqlite3_free(normalized);
    return rc;
  }

  int analyzedCount = 0;
  int rc = plan_query_analyze_count(plan->tokenizer, base, nBase, &analyzedCount);
  if (rc != SQLITE_OK) {
    return rc;
  }
  if (analyzedCount == 0) {
    return execute_query_build_unwrapped_single_term_expression("__nohit__", 9, compiledOut, nCompiledOut);
  }
  return execute_query_build_unwrapped_single_term_expression(base, nBase, compiledOut, nCompiledOut);
}

static int plan_query_compile_leaf(
    void *ctx, const ParsedSimpleAstClause *clause, int runtimeFieldKind, char **compiledOut, int *nCompiledOut) {
  PlanQueryContext *plan = (PlanQueryContext *)ctx;
  if (token_equals_ascii_ci(clause->kind, clause->nKind, "token")) {
    return plan_query_compile_token(plan, clause, runtimeFieldKind, compiledOut, nCompiledOut);
  }
  if (token_equals_ascii_ci(clause->kind, clause->nKind, "phrase") ||
      token_equals_ascii_ci(clause->kind, clause->nKind, "phrase_slop") ||
      token_equals_ascii_ci(clause->kind, clause->nKind, "field_phrase") ||
      token_equals_ascii_ci(clause->kind, clause->nKind, "field_phrase_slop")) {
    return plan_query_compile_phrase(plan, clause, runtimeFieldKind, compiledOut, nCompiledOut);
  }
  return SQLITE_NOTFOUND;
}

static int plan_query_compile_query(
    PlanQueryContext *plan, const char *query, int nQuery, int runtimeFieldKind, char **compiledOut, int *nCompiledOut, int depth) {
  return execute_query_compile_ast_expression(
      query, nQuery, runtimeFieldKind, plan_query_compile_leaf, plan, SQLITE_NOTFOUND, depth, compiledOut, nCompiledOut);
}

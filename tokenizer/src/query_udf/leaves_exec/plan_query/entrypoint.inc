static int plan_query_append_raw_terms(
    const char *text, int nText, char **json, int *nJson, int *jsonCap, int *termCount) {
  int idx = 0;
  int tokenStart = -1;
  while (idx < nText) {
    const HonorificExpansion *expansion = find_honorific_expansion(text + idx, nText - idx);
    if (expansion != NULL) {
      if (tokenStart >= 0) {
        if (*termCount > 0 && json_builder_append_char(json, nJson, jsonCap, ',') != SQLITE_OK) {
          return SQLITE_NOMEM;
        }
        int rc = json_builder_append_escaped_string(json, nJson, jsonCap, text + tokenStart, idx - tokenStart);
        if (rc != SQLITE_OK) {
          return rc;
        }
        (*termCount)++;
        tokenStart = -1;
      }
      if (*termCount > 0 && json_builder_append_char(json, nJson, jsonCap, ',') != SQLITE_OK) {
        return SQLITE_NOMEM;
      }
      int rc = json_builder_append_escaped_string(json, nJson, jsonCap, expansion->glyph, expansion->glyphLen);
      if (rc != SQLITE_OK) {
        return rc;
      }
      (*termCount)++;
      idx += expansion->glyphLen;
      continue;
    }
    uint32_t cp = 0;
    int consumed = 0;
    int decoded = utf8_decode(text + idx, nText - idx, &consumed, &cp);
    if (!decoded || consumed <= 0) {
      consumed = 1;
    }
    if (decoded && snippet_query_is_token_codepoint(cp)) {
      if (tokenStart < 0) {
        tokenStart = idx;
      }
    } else if (tokenStart >= 0) {
      if (*termCount > 0 && json_builder_append_char(json, nJson, jsonCap, ',') != SQLITE_OK) {
        return SQLITE_NOMEM;
      }
      int rc = json_builder_append_escaped_string(json, nJson, jsonCap, text + tokenStart, idx - tokenStart);
      if (rc != SQLITE_OK) {
        return rc;
      }
      (*termCount)++;
      tokenStart = -1;
    }
    idx += consumed;
  }
  if (tokenStart >= 0) {
    if (*termCount > 0 && json_builder_append_char(json, nJson, jsonCap, ',') != SQLITE_OK) {
      return SQLITE_NOMEM;
    }
    int rc = json_builder_append_escaped_string(json, nJson, jsonCap, text + tokenStart, nText - tokenStart);
    if (rc != SQLITE_OK) {
      return rc;
    }
    (*termCount)++;
  }
  return SQLITE_OK;
}

static int plan_query_append_verify_phrases(
    const char *query, int nQuery, char **json, int *nJson, int *jsonCap, int *count) {
  SnippetQueryStringList phrases;
  memset(&phrases, 0, sizeof(phrases));
  int rc = snippet_query_collect_from_query_recursive(query, nQuery, NULL, &phrases, 0);
  for (int i = 0; rc == SQLITE_OK && i < phrases.count; i++) {
    char *termsJson = NULL;
    int nTermsJson = 0;
    int termsJsonCap = 0;
    int termCount = 0;
    rc = plan_query_append_raw_terms(
        phrases.items[i], phrases.itemLens[i], &termsJson, &nTermsJson, &termsJsonCap, &termCount);
    if (rc != SQLITE_OK) {
      sqlite3_free(termsJson);
      break;
    }
    if (termCount == 0) {
      sqlite3_free(termsJson);
      continue;
    }
    if (*count > 0) {
      rc = json_builder_append_char(json, nJson, jsonCap, ',');
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_bytes(json, nJson, jsonCap, "{\"mode\":\"phrase\",\"raw_terms\":[", 30);
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_bytes(json, nJson, jsonCap, termsJson, nTermsJson);
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_bytes(json, nJson, jsonCap, "]}", 2);
      (*count)++;
    }
    sqlite3_free(termsJson);
  }
  snippet_query_string_list_free(&phrases);
  return rc;
}

static const char *plan_query_verify_unsupported_reason(const char *query, int nQuery, int depth) {
  if (depth > 16) {
    return "verification_boolean";
  }
  ParsedSimpleAstClause clauses[2];
  int clauseCount = 0;
  if (!parse_simple_query_ast_clauses(query, nQuery, &clauses[0], &clauses[1], &clauseCount)) {
    return "verification_boolean";
  }
  int hasShould = 0;
  for (int i = 0; i < clauseCount; i++) {
    hasShould |= token_equals_ascii_ci(clauses[i].occur, clauses[i].nOccur, "SHOULD");
  }
  if (clauseCount > 1 && hasShould) {
    return "verification_boolean";
  }
  for (int i = 0; i < clauseCount; i++) {
    if (clauses[i].nOccur == 8 && token_equals_ascii_ci(clauses[i].occur, clauses[i].nOccur, "MUST_NOT")) {
      return "verification_boolean";
    }
    if (token_equals_ascii_ci(clauses[i].kind, clauses[i].nKind, "phrase_slop") ||
        token_equals_ascii_ci(clauses[i].kind, clauses[i].nKind, "field_phrase_slop")) {
      return "verification_slop";
    }
    if (token_equals_ascii_ci(clauses[i].kind, clauses[i].nKind, "group")) {
      const char *reason = plan_query_verify_unsupported_reason(clauses[i].value, clauses[i].nValue, depth + 1);
      if (reason != NULL) {
        return reason;
      }
    }
    if (token_equals_ascii_ci(clauses[i].kind, clauses[i].nKind, "field_group") ||
        token_equals_ascii_ci(clauses[i].kind, clauses[i].nKind, "field_group_boost")) {
      const char *scope = NULL;
      int nScope = 0;
      const char *inner = NULL;
      int nInner = 0;
      if (!execute_query_parse_field_group_clause_value(&clauses[i], &scope, &nScope, &inner, &nInner)) {
        return "verification_boolean";
      }
      const char *reason = plan_query_verify_unsupported_reason(inner, nInner, depth + 1);
      if (reason != NULL) {
        return reason;
      }
    }
  }
  return NULL;
}

static void udf_sqlite_tokenizer_ar_plan_query_json(sqlite3_context *context, int argc, sqlite3_value **argv) {
  if (argc != 3) {
    sqlite3_result_error(context, "sqlite_tokenizer_ar_plan_query_json expects 3 arguments", -1);
    return;
  }
  if (sqlite3_value_type(argv[0]) == SQLITE_NULL || sqlite3_value_type(argv[1]) == SQLITE_NULL) {
    sqlite3_result_error(context, "query and field are required", -1);
    return;
  }
  const char *query = (const char *)sqlite3_value_text(argv[0]);
  int nQuery = sqlite3_value_bytes(argv[0]);
  const char *field = (const char *)sqlite3_value_text(argv[1]);
  int nField = sqlite3_value_bytes(argv[1]);
  int requestedFieldKind = execute_query_field_kind(field, nField);
  if (query == NULL || nQuery <= 0 || requestedFieldKind == 0) {
    sqlite3_result_error(context, "non-empty query and field=page|title|both are required", -1);
    return;
  }
  const char *optionsJson = NULL;
  int nOptionsJson = 0;
  if (sqlite3_value_type(argv[2]) != SQLITE_NULL) {
    optionsJson = (const char *)sqlite3_value_text(argv[2]);
    nOptionsJson = sqlite3_value_bytes(argv[2]);
  }

  PlanQueryContext plan;
  memset(&plan, 0, sizeof(plan));
  int rc = plan_query_parse_options(optionsJson, nOptionsJson, &plan.options);
  if (rc != SQLITE_OK) {
    json_string_array_free(&plan.options.tokenizerArgs);
    sqlite3_result_error(context, "invalid options_json", -1);
    return;
  }
  const char *malformed = parse_query_ast_classify_malformed_query(query, nQuery);
  if (malformed != NULL) {
    if (!plan.options.lenientParseErrors) {
      json_string_array_free(&plan.options.tokenizerArgs);
      sqlite3_result_error(context, malformed, -1);
      return;
    }
    query = "__nohit__";
    nQuery = 9;
  }
  Fts5Tokenizer *tokenizer = NULL;
  rc = arabic_create(
      NULL, (const char **)plan.options.tokenizerArgs.items, plan.options.tokenizerArgs.count, &tokenizer);
  if (rc != SQLITE_OK) {
    json_string_array_free(&plan.options.tokenizerArgs);
    sqlite3_result_error(context, "invalid tokenizer_args", -1);
    return;
  }
  plan.tokenizer = (ArabicTokenizer *)tokenizer;

  char *pageCompiled = NULL;
  int nPageCompiled = 0;
  char *titleCompiled = NULL;
  int nTitleCompiled = 0;
  const char *unsupported = NULL;
  int nUnsupported = 0;
  if (!plan.options.ignoreDiacritics || !plan.options.ignoreHamzaForms || !plan.options.ignoreLetterForms ||
      !plan.options.ignoreDigitForms) {
    unsupported = "strict_verification";
    nUnsupported = 19;
  } else if (find_unescaped_char_span(query, 0, nQuery, '^') >= 0) {
    unsupported = "boost_ranking";
    nUnsupported = 13;
  }
  if (unsupported == NULL && (requestedFieldKind == 1 || requestedFieldKind == 3)) {
    plan.field = "page";
    plan.nField = 4;
    plan.expansionId = 0;
    rc = plan_query_compile_query(&plan, query, nQuery, 1, &pageCompiled, &nPageCompiled, 0);
  }
  if (unsupported == NULL && rc == SQLITE_OK && (requestedFieldKind == 2 || requestedFieldKind == 3)) {
    plan.field = "title";
    plan.nField = 5;
    plan.expansionId = 0;
    rc = plan_query_compile_query(&plan, query, nQuery, 2, &titleCompiled, &nTitleCompiled, 0);
  }

  char *verifyJson = NULL;
  int nVerifyJson = 0;
  int verifyJsonCap = 0;
  int verifyCount = 0;
  if (rc == SQLITE_NOTFOUND) {
    unsupported = "query_shape";
    nUnsupported = 11;
    rc = SQLITE_OK;
  }
  if (rc == SQLITE_OK && unsupported == NULL) {
    rc = plan_query_append_verify_phrases(query, nQuery, &verifyJson, &nVerifyJson, &verifyJsonCap, &verifyCount);
  }
  const char *verifyUnsupported = NULL;
  if (rc == SQLITE_OK && verifyCount > 0) {
    verifyUnsupported = plan_query_verify_unsupported_reason(query, nQuery, 0);
  }
  if (verifyUnsupported != NULL) {
    unsupported = verifyUnsupported;
    nUnsupported = (int)strlen(verifyUnsupported);
  }
  if (unsupported != NULL) {
    sqlite3_free(pageCompiled);
    sqlite3_free(titleCompiled);
    pageCompiled = NULL;
    titleCompiled = NULL;
    sqlite3_free(plan.expandJson);
    plan.expandJson = NULL;
    plan.nExpandJson = 0;
    sqlite3_free(verifyJson);
    verifyJson = NULL;
    nVerifyJson = 0;
  }
  if (rc != SQLITE_OK) {
    arabic_delete(tokenizer);
    json_string_array_free(&plan.options.tokenizerArgs);
    sqlite3_free(pageCompiled);
    sqlite3_free(titleCompiled);
    sqlite3_free(plan.expandJson);
    sqlite3_free(verifyJson);
    sqlite3_result_error(context, "unsupported query shape", -1);
    return;
  }

  char *json = NULL;
  int nJson = 0;
  int jsonCap = 0;
  rc = json_builder_append_bytes(&json, &nJson, &jsonCap, "{\"match_template\":{", 19);
  int wroteField = 0;
  if (rc == SQLITE_OK && pageCompiled != NULL) {
    rc = json_builder_append_bytes(&json, &nJson, &jsonCap, "\"page\":", 7);
    if (rc == SQLITE_OK) {
      rc = json_builder_append_escaped_string(&json, &nJson, &jsonCap, pageCompiled, nPageCompiled);
      wroteField = 1;
    }
  }
  if (rc == SQLITE_OK && titleCompiled != NULL) {
    if (wroteField) {
      rc = json_builder_append_char(&json, &nJson, &jsonCap, ',');
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_bytes(&json, &nJson, &jsonCap, "\"title\":", 8);
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_escaped_string(&json, &nJson, &jsonCap, titleCompiled, nTitleCompiled);
    }
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &nJson, &jsonCap, "},\"verify\":[", 12);
  }
  if (rc == SQLITE_OK && verifyJson != NULL) {
    rc = json_builder_append_bytes(&json, &nJson, &jsonCap, verifyJson, nVerifyJson);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &nJson, &jsonCap, "],\"expand\":[", 12);
  }
  if (rc == SQLITE_OK && plan.expandJson != NULL) {
    rc = json_builder_append_bytes(&json, &nJson, &jsonCap, plan.expandJson, plan.nExpandJson);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &nJson, &jsonCap, "],\"unsupported\":[", 17);
  }
  if (rc == SQLITE_OK && unsupported != NULL) {
    rc = json_builder_append_escaped_string(&json, &nJson, &jsonCap, unsupported, nUnsupported);
  }
  if (rc == SQLITE_OK) {
    rc = json_builder_append_bytes(&json, &nJson, &jsonCap, "]}", 2);
  }
  arabic_delete(tokenizer);
  json_string_array_free(&plan.options.tokenizerArgs);
  sqlite3_free(pageCompiled);
  sqlite3_free(titleCompiled);
  sqlite3_free(plan.expandJson);
  sqlite3_free(verifyJson);
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return;
  }
  json[nJson] = '\0';
  sqlite3_result_text(context, json, nJson, sqlite3_free);
}

static void result_simple_token_or_binary_ast_json(sqlite3_context *context, const char *query, int nQuery) {
  const char *tokens[4] = {0};
  int tokenLens[4] = {0};
  int tokenCount = 0;
  int splitRc = split_ascii_space_tokens(query, nQuery, tokens, tokenLens, 4, &tokenCount);
  if (splitRc != SQLITE_OK) {
    result_parse_query_ast_unsupported(context);
    return;
  }
  if (tokenCount == 0) {
    sqlite3_result_text(context, "{\"clauses\":[]}", -1, SQLITE_STATIC);
    return;
  }

  if (tokenCount != 1 && tokenCount != 3) {
    result_parse_query_ast_unsupported(context);
    return;
  }
  for (int i = 0; i < tokenCount; i++) {
    for (int j = 0; j < tokenLens[i]; j++) {
      char ch = tokens[i][j];
      if (ch == '"' || ch == '(' || ch == ')' || ch == ':' || ch == '\t' || ch == '\\') {
        result_parse_query_ast_unsupported(context);
        return;
      }
    }
  }

  char *json = NULL;
  int jsonLen = 0;
  int jsonCap = 0;
  int rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "{\"clauses\":[", 12);
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return;
  }

  if (tokenCount == 1) {
    const char *token = tokens[0];
    int nToken = tokenLens[0];
    const char *occur = "SHOULD";
    if (nToken > 0 && token[0] == '+') {
      occur = "MUST";
      token++;
      nToken--;
    } else if (nToken > 0 && token[0] == '-') {
      occur = "MUST_NOT";
      token++;
      nToken--;
    }
    if (nToken <= 0 || token_equals_ascii_ci(token, nToken, "AND") || token_equals_ascii_ci(token, nToken, "OR") ||
        token_equals_ascii_ci(token, nToken, "NOT")) {
      sqlite3_free(json);
      result_parse_query_ast_unsupported(context);
      return;
    }
    rc = json_builder_append_clause_token(&json, &jsonLen, &jsonCap, occur, token, nToken);
    if (rc != SQLITE_OK) {
      sqlite3_free(json);
      sqlite3_result_error_nomem(context);
      return;
    }
  } else {
    const char *left = tokens[0];
    int nLeft = tokenLens[0];
    const char *op = tokens[1];
    int nOp = tokenLens[1];
    const char *right = tokens[2];
    int nRight = tokenLens[2];
    if (nLeft <= 0 || nRight <= 0) {
      sqlite3_free(json);
      result_parse_query_ast_unsupported(context);
      return;
    }
    const char *leftOccur;
    const char *rightOccur;
    if (token_equals_ascii_ci(op, nOp, "AND")) {
      leftOccur = "MUST";
      rightOccur = "MUST";
    } else if (token_equals_ascii_ci(op, nOp, "OR")) {
      leftOccur = "SHOULD";
      rightOccur = "SHOULD";
    } else if (token_equals_ascii_ci(op, nOp, "NOT")) {
      leftOccur = "SHOULD";
      rightOccur = "MUST_NOT";
    } else {
      sqlite3_free(json);
      result_parse_query_ast_unsupported(context);
      return;
    }

    rc = json_builder_append_clause_token(&json, &jsonLen, &jsonCap, leftOccur, left, nLeft);
    if (rc == SQLITE_OK) {
      rc = json_builder_append_char(&json, &jsonLen, &jsonCap, ',');
    }
    if (rc == SQLITE_OK) {
      rc = json_builder_append_clause_token(&json, &jsonLen, &jsonCap, rightOccur, right, nRight);
    }
    if (rc != SQLITE_OK) {
      sqlite3_free(json);
      sqlite3_result_error_nomem(context);
      return;
    }
  }

  rc = json_builder_append_bytes(&json, &jsonLen, &jsonCap, "]}", 2);
  if (rc != SQLITE_OK) {
    sqlite3_free(json);
    sqlite3_result_error_nomem(context);
    return;
  }
  json[jsonLen] = '\0';
  sqlite3_result_text(context, json, jsonLen, sqlite3_free);
}

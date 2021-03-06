#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <mpdecimal.h>
#include <utf8proc.h>

#include "config.h"
#include "rune.h"
#include "sslice.h"
#include "token.h"
#include "lexer.h"
#include "utils.h"

const rune MathOpValues[MATHOP_MAX] = {
    '+', '-', '*', '/', '%', '^',
};

const rune SymbolValues[SYMBOL_MAX] = {
    '(', ')', '[', ']', '{', '}', ',', '.', '\'', '`', '"', '|'
};

const rune WhitespaceValues[WHITESPACE_MAX] = {
    ' ', '\t', '\r', '\n',
};

const char *BoolOpValues[BOOLOP_MAX] = {
    "==", "!=", ">", ">=", "<", "<=", "&&", "||"
};

const char *KeywordValues[KEYWORD_MAX] = {
    "include", "if", "elif", "else", "endif", "for", "in", "endfor", "raw",
    "endraw", "range"
};

static SSliceStatus seek_to_next_code_tag_start(SSlice *s) {
    SSlice cursor;
    SSliceStatus res;

    sslice_shallow_copy(&cursor, s);

    while (true) {
        if (sslice_starts_with(&cursor, "{{")) {
            break;
        }

        res = sslice_advance_rune(&cursor);

        if (res != SSLICE_OK) {
            return res;
        }
    }

    sslice_shallow_copy(s, &cursor);

    return SSLICE_OK;
}

static bool tag_is_raw(SSlice *s) {
    return sslice_starts_with(s, "{{ raw }}");
}

static SSliceStatus seek_to_endraw(SSlice *s) {
    SSlice cursor;
    SSliceStatus sstatus;

    sslice_shallow_copy(&cursor, s);

    sstatus = sslice_seek_to_string(&cursor, "{{ endraw }}");

    if (sstatus != SSLICE_OK) {
        return sstatus;
    }

    sslice_shallow_copy(s, &cursor);

    return SSLICE_OK;
}

static bool lexer_found_code_tag_close(Lexer *lexer) {
    Token *previous_token;
    Token *current_token;

    if (token_queue_count(&lexer->tokens) < 2) {
        return false;
    }

    previous_token = lexer_get_previous_token(lexer);

    if (!previous_token) {
        return false;
    }

    current_token = lexer_get_current_token(lexer);

    if (!current_token) {
        return false;
    }

    if (previous_token->type != TOKEN_SYMBOL) {
        return false;
    }

    if (previous_token->as.symbol != SYMBOL_CBRACE) {
        return false;
    }

    if (current_token->type != TOKEN_SYMBOL) {
        return false;
    }

    if (current_token->as.symbol != SYMBOL_CBRACE) {
        return false;
    }

    return true;
}

static LexerStatus lexer_handle_number(Lexer *lexer) {
    SSlice start;
    SSliceStatus sstatus;
    bool found_at_least_one_digit = false;
    Token *token;
    uint32_t res = 0;

    sslice_shallow_copy(&start, &lexer->data);

    while (true) {
        rune r;
        bool is_digit;

        sstatus = sslice_get_first_rune(&lexer->data, &r);

        if (sstatus != SSLICE_OK) {
            if (sstatus == SSLICE_END) {
                break;
            }

            return sstatus;
        }

        is_digit = rune_is_digit(r);

        if (is_digit) {
            found_at_least_one_digit = true;
        }

        if (!(is_digit || (r == ',') || (r == '.'))) {
            break;
        }

        sslice_pop_rune(&lexer->data, NULL);
    }

    if (!found_at_least_one_digit) {
        return LEXER_INVALID_NUMBER_FORMAT;
    }

    sstatus = sslice_truncate_at_subslice(&start, &lexer->data);

    if (sstatus != SSLICE_OK) {
        return sstatus;
    }

    char *num = sslice_to_c_string(&start);

    if (!num) {
        return LEXER_DATA_MEMORY_EXHAUSTED;
    }

    token = token_queue_push_new(&lexer->tokens);
    token->type = TOKEN_NUMBER;
    token->location = lexer->data.data;
    token->as.number = mpd_new(&lexer->mpd_ctx);

    if (!token->as.number) {
        free(num);

        return LEXER_DATA_MEMORY_EXHAUSTED;
    }

    mpd_qset_string(token->as.number, num, &lexer->mpd_ctx, &res);

    free(num);

    if (res != 0) {
        return LEXER_INVALID_NUMBER_FORMAT;
    }

    return LEXER_OK;
}

static LexerStatus lexer_handle_keyword_or_identifier(Lexer *lexer) {
    SSlice start;
    Token *token;
    SSliceStatus sstatus;

    sslice_shallow_copy(&start, &lexer->data);

    sstatus = sslice_truncate_at_whitespace(&start);

    if (sstatus != SSLICE_OK) {
        return sstatus;
    }

#if 0
    if (!validate_identifier(&start)) {
        return LEXER_INVALID_IDENTIFIER_FORMAT;
    }
#endif

    for (Keyword kw = KEYWORD_FIRST; kw < KEYWORD_MAX; kw++) {
        if (sslice_equals(&start, KeywordValues[kw])) {
            token = token_queue_push_new(&lexer->tokens);

            token->type = TOKEN_KEYWORD;
            token->location = lexer->data.data;
            token->as.keyword = kw;

            return sslice_advance_runes(&lexer->data, start.len);
        }
    }

    token = token_queue_push_new(&lexer->tokens);
    token->type = TOKEN_IDENTIFIER;
    token->location = start.data;
    sslice_shallow_copy(&token->as.identifier, &start);

    return sslice_seek_past_subslice(&lexer->data, &start);
}

static LexerStatus lexer_handle_string(Lexer *lexer, rune r) {
    SSlice start;
    Token *token;
    SSliceStatus sstatus;

    sslice_shallow_copy(&start, &lexer->data);

    sstatus = sslice_advance_rune(&start);

    switch (sstatus) {
        case SSLICE_END:
        case SSLICE_MEMORY_EXHAUSTED:
        case SSLICE_NOT_ASSIGNED:
        case SSLICE_INVALID_OPTS:
            return sstatus;
        default:
            break;
    }

    sstatus = sslice_truncate_at(&start, r);

    switch (sstatus) {
        case SSLICE_END:
        case SSLICE_MEMORY_EXHAUSTED:
        case SSLICE_NOT_ASSIGNED:
        case SSLICE_INVALID_OPTS:
            return sstatus;
        default:
            break;
    }

    token = token_queue_push_new(&lexer->tokens);
    token->type = TOKEN_STRING;
    token->location = lexer->data.data;
    sslice_shallow_copy(&token->as.string, &start);

    sstatus = sslice_seek_past_subslice(&lexer->data, &start);

    if (sstatus != SSLICE_OK) {
        return sstatus;
    }

    return sslice_advance_runes(&lexer->data, 1);
}

static LexerStatus lexer_handle_boolop(Lexer *lexer, rune r, bool *handled) {
    SSliceStatus sstatus;
    SSlice start;

    sslice_shallow_copy(&start, &lexer->data);

    switch (r) {
        case '=':
            sstatus = sslice_advance_rune(&lexer->data);

            if (sstatus != SSLICE_OK) {
                return sstatus;
            }

            if (sslice_pop_rune_if_equals(&lexer->data, '=')) {
                Token *token = token_queue_push_new(&lexer->tokens);

                token->type = TOKEN_BOOLOP;
                token->location = start.data;
                token->as.bool_op = BOOLOP_EQUAL;
            }
            else {
                Token *token = token_queue_push_new(&lexer->tokens);

                token->type = TOKEN_UNKNOWN;
                token->location = start.data;
                token->as.literal = r;
            }
            *handled = true;
            return LEXER_OK;
        case '<':
            sstatus = sslice_advance_rune(&lexer->data);

            if (sstatus != SSLICE_OK) {
                return sstatus;
            }

            if (sslice_pop_rune_if_equals(&lexer->data, '=')) {
                Token *token = token_queue_push_new(&lexer->tokens);

                token->type = TOKEN_BOOLOP;
                token->location = start.data;
                token->as.bool_op = BOOLOP_LESS_THAN_OR_EQUAL;
            }
            else {
                Token *token = token_queue_push_new(&lexer->tokens);

                token->type = TOKEN_UNARY_BOOLOP;
                token->location = start.data;
                token->as.unary_bool_op = BOOLOP_LESS_THAN;
            }
            *handled = true;
            return LEXER_OK;
        case '>':
            sstatus = sslice_advance_rune(&lexer->data);

            if (sstatus != SSLICE_OK) {
                return sstatus;
            }

            if (sslice_pop_rune_if_equals(&lexer->data, '=')) {
                Token *token = token_queue_push_new(&lexer->tokens);

                token->type = TOKEN_BOOLOP;
                token->location = start.data;
                token->as.bool_op = BOOLOP_GREATER_THAN_OR_EQUAL;
            }
            else {
                Token *token = token_queue_push_new(&lexer->tokens);

                token->type = TOKEN_UNARY_BOOLOP;
                token->as.unary_bool_op = BOOLOP_GREATER_THAN;
            }
            *handled = true;
            return LEXER_OK;
        case '&':
            sstatus = sslice_advance_rune(&lexer->data);

            if (sstatus != SSLICE_OK) {
                return sstatus;
            }

            if (sslice_pop_rune_if_equals(&lexer->data, '&')) {
                Token *token = token_queue_push_new(&lexer->tokens);

                token->type = TOKEN_BOOLOP;
                token->location = start.data;
                token->as.bool_op = BOOLOP_AND;
            }
            else {
                Token *token = token_queue_push_new(&lexer->tokens);

                token->type = TOKEN_UNKNOWN;
                token->location = start.data;
                token->as.literal = r;
            }
            *handled = true;
            return LEXER_OK;
        case '|':
            sstatus = sslice_advance_rune(&lexer->data);

            if (sstatus != SSLICE_OK) {
                return sstatus;
            }

            if (sslice_pop_rune_if_equals(&lexer->data, '|')) {
                Token *token = token_queue_push_new(&lexer->tokens);

                token->type = TOKEN_BOOLOP;
                token->location = start.data;
                token->as.bool_op = BOOLOP_OR;
            }
            else {
                Token *token = token_queue_push_new(&lexer->tokens);

                token->type = TOKEN_UNKNOWN;
                token->location = start.data;
                token->as.literal = r;
            }
            *handled = true;
            return LEXER_OK;
        case '!':
            sstatus = sslice_advance_rune(&lexer->data);

            if (sstatus != SSLICE_OK) {
                return sstatus;
            }

            if (sslice_pop_rune_if_equals(&lexer->data, '=')) {
                Token *token = token_queue_push_new(&lexer->tokens);

                token->type = TOKEN_BOOLOP;
                token->location = start.data;
                token->as.bool_op = BOOLOP_NOT_EQUAL;
            }
            else {
                Token *token = token_queue_push_new(&lexer->tokens);

                token->type = TOKEN_UNARY_BOOLOP;
                token->location = start.data;
                token->as.unary_bool_op = UBOOLOP_NOT;
            }
            *handled = true;
            return LEXER_OK;
        default:
            *handled = false;
            return false;
    }
}

static LexerStatus lexer_load_next_code_token(Lexer *lexer) {
    rune r;
    LexerStatus lstatus;
    SSliceStatus sstatus;

    while (true) {
        if (sslice_empty(&lexer->data)) {
            return LEXER_END;
        }

        sstatus = sslice_get_first_rune(&lexer->data, &r);

        if (sstatus != SSLICE_OK) {
            return sstatus;
        }

        bool found_whitespace = false;

        for (Whitespace ws = WHITESPACE_FIRST; ws < WHITESPACE_MAX; ws++) {
            if (r == WhitespaceValues[ws]) {
                Token *token = token_queue_push_new(&lexer->tokens);

                token->type = TOKEN_WHITESPACE;
                token->location = lexer->data.data;
                token->as.whitespace = ws;

                found_whitespace = true;

                sstatus = sslice_advance_runes(&lexer->data, 1);

                if (sstatus != SSLICE_OK) {
                    return sstatus;
                }

                break;
            }

        }

        if (!found_whitespace) {
            break;
        }

        return LEXER_OK;
    }

    if (rune_is_digit(r) || r == '-' || r == '.') {
        return lexer_handle_number(lexer);
    }

    if (rune_is_alpha(r) || r == '_') {
        return lexer_handle_keyword_or_identifier(lexer);
    }

    if (r == '\'' || r == '`' || r == '"') {
        return lexer_handle_string(lexer, r);
    }

    bool handled = false;

    lstatus = lexer_handle_boolop(lexer, r, &handled);

    if (lstatus != LEXER_OK) {
        return lstatus;
    }

    if (handled) {
        return LEXER_OK;
    }

    for (MathOp m = MATHOP_FIRST; m < MATHOP_MAX; m++) {
        if (r == MathOpValues[m]) {
            Token *token = token_queue_push_new(&lexer->tokens);

            token->type = TOKEN_MATHOP;
            token->location = lexer->data.data;
            token->as.math_op = m;

            return sslice_advance_rune(&lexer->data);
        }
    }

    for (Symbol s = SYMBOL_FIRST; s < SYMBOL_MAX; s++) {
        if (r == SymbolValues[s]) {
            Token *token = token_queue_push_new(&lexer->tokens);

            token->type = TOKEN_SYMBOL;
            token->location = lexer->data.data;
            token->as.symbol = s;

            if (lexer_found_code_tag_close(lexer)) {
                lexer->in_code = false;
            }

            return sslice_advance_rune(&lexer->data);
        }
    }

    return LEXER_UNKNOWN_TOKEN;
}

void lexer_init(Lexer *lexer) {
    lexer_clear(lexer);
    mpd_maxcontext(&lexer->mpd_ctx);
}

void lexer_clear(Lexer *lexer) {
    sslice_clear(&lexer->data);
    sslice_clear(&lexer->tag);
    lexer->in_code = false;
    token_queue_clear(&lexer->tokens);
}

void lexer_set_data(Lexer *lexer, SSlice *data) {
    lexer_clear(lexer);

    sslice_shallow_copy(&lexer->data, data);
}

LexerStatus lexer_load_next(Lexer *lexer) {
    SSlice start;
    SSliceStatus sstatus;

    if (sslice_empty(&lexer->data)) {
        return LEXER_END;
    }

    sslice_shallow_copy(&start, &lexer->data);

    if (lexer->in_code) {
        return lexer_load_next_code_token(lexer);
    }

    sstatus = seek_to_next_code_tag_start(&lexer->data);

    if (sstatus != SSLICE_OK) {
        if (sstatus == SSLICE_END) {
            Token *token = token_queue_push_new(&lexer->tokens);

            token->type = TOKEN_TEXT;
            token->location = lexer->data.data;
            sslice_shallow_copy(&token->as.text, &start);

            sslice_seek_past_subslice(&lexer->data, &token->as.text);

            return LEXER_OK;
        }

        return sstatus;
    }

    if (lexer->data.data != start.data) {
        Token *token = token_queue_push_new(&lexer->tokens);

        token->type = TOKEN_TEXT;
        token->location = start.data;
        sslice_truncate_at_subslice(&start, &lexer->data);
        sslice_shallow_copy(&token->as.text, &start);

        return LEXER_OK;
    }

    if (tag_is_raw(&lexer->data)) {
        Token *token;

        sslice_shallow_copy(&start, &lexer->data);
        
        sstatus = seek_to_endraw(&lexer->data);

        if (sstatus != SSLICE_OK) {
            return sstatus;
        }

        token = token_queue_push_new(&lexer->tokens);

        token->type = TOKEN_TEXT;
        token->location = start.data;

        sslice_truncate_at_subslice(&start, &lexer->data);

        sslice_shallow_copy(&token->as.text, &start);

        /* {{ raw }} is 9 runes */
        sstatus = sslice_advance_runes(&token->as.text, 9);

        if (sstatus != SSLICE_OK) {
            return sstatus;
        }

        /* {{ endraw }} is 12 runes */
        sslice_truncate_runes(&token->as.text, 12);

        if (sstatus != SSLICE_OK) {
            return sstatus;
        }

        sslice_advance_runes(&lexer->data, 12);

        return LEXER_OK;
    }

    lexer->in_code = true;
    return lexer_load_next_code_token(lexer);
}

Token* lexer_get_previous_token(Lexer *lexer) {
    TokenQueue *token_queue = &lexer->tokens;

    if (token_queue_count(token_queue) < 2) {
        return NULL;
    }

    if (token_queue->tail > 0) {
        return &token_queue->tokens[token_queue->tail - 1];
    }

    return &token_queue->tokens[TOKEN_QUEUE_SIZE - 1];
}

Token* lexer_get_current_token(Lexer *lexer) {
    TokenQueue *token_queue = &lexer->tokens;

    if (token_queue_empty(token_queue)) {
        return NULL;
    }

    return &token_queue->tokens[token_queue->tail];
}

/* vi: set et ts=4 sw=4: */


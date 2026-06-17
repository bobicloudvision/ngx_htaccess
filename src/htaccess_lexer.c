
#include "htaccess_directives.h"


static u_char *
htaccess_skip_line(u_char *p, u_char *end)
{
    while (p < end && *p != '\n' && *p != '\r') {
        p++;
    }

    while (p < end && (*p == '\n' || *p == '\r')) {
        p++;
    }

    return p;
}


static u_char *
htaccess_skip_space(u_char *p, u_char *end)
{
    while (p < end && (*p == ' ' || *p == '\t')) {
        p++;
    }

    return p;
}


static ngx_int_t
htaccess_read_quoted(u_char *p, u_char *end, ngx_str_t *out, u_char **next)
{
    u_char  quote;
    u_char *start;

    if (p >= end) {
        return NGX_ERROR;
    }

    quote = *p++;
    start = p;

    while (p < end && *p != quote) {
        if (*p == '\\' && p + 1 < end) {
            p += 2;
            continue;
        }
        p++;
    }

    if (p >= end) {
        return NGX_ERROR;
    }

    out->data = start;
    out->len = p - start;
    *next = p + 1;

    return NGX_OK;
}


ngx_int_t
htaccess_lex_token(u_char *p, u_char *end, ngx_str_t *token, u_char **next)
{
    p = htaccess_skip_space(p, end);

    if (p >= end || *p == '#') {
        token->len = 0;
        *next = htaccess_skip_line(p, end);
        return NGX_DONE;
    }

    if (*p == '"') {
        return htaccess_read_quoted(p, end, token, next);
    }

    if (*p == '<') {
        token->data = p;
        while (p < end && *p != '>') {
            p++;
        }
        if (p >= end) {
            return NGX_ERROR;
        }
        token->len = p - token->data + 1;
        *next = p + 1;
        return NGX_OK;
    }

    token->data = p;
    while (p < end && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r'
           && *p != '#')
    {
        if (*p == '\\' && p + 1 < end) {
            p += 2;
            continue;
        }
        p++;
    }

    token->len = p - token->data;
    *next = p;

    return token->len ? NGX_OK : NGX_DONE;
}


ngx_int_t
htaccess_lex_line(u_char **pos, u_char *end, ngx_array_t *tokens,
    ngx_pool_t *pool)
{
    u_char    *p;
    u_char    *next;
    ngx_str_t  token;
    ngx_str_t *t;
    ngx_int_t  rc;

    p = *pos;

    while (p < end) {
        p = htaccess_skip_space(p, end);
        if (p >= end || *p == '#') {
            break;
        }

        rc = htaccess_lex_token(p, end, &token, &next);
        if (rc == NGX_ERROR) {
            return NGX_ERROR;
        }
        if (rc == NGX_DONE) {
            break;
        }

        t = ngx_array_push(tokens);
        if (t == NULL) {
            return NGX_ERROR;
        }

        t->len = token.len;
        t->data = ngx_pnalloc(pool, token.len);
        if (t->data == NULL) {
            return NGX_ERROR;
        }
        ngx_memcpy(t->data, token.data, token.len);

        p = next;
    }

    *pos = htaccess_skip_line(p, end);

    return NGX_OK;
}

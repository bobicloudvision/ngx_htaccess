
#include "htaccess_directives.h"


static ngx_str_t
htaccess_get_http_header(ngx_http_request_t *r, ngx_str_t *name)
{
    ngx_list_part_t  *part;
    ngx_table_elt_t  *h;
    ngx_uint_t        i;
    ngx_str_t         empty;

    empty.len = 0;
    empty.data = NULL;

    part = &r->headers_in.headers.part;
    while (part) {
        h = part->elts;
        for (i = 0; i < part->nelts; i++) {
            if (h[i].key.len == name->len
                && ngx_strncasecmp(h[i].key.data, name->data, name->len) == 0)
            {
                return h[i].value;
            }
        }
        part = part->next;
    }

    if (name->len == 4 && ngx_strncasecmp(name->data, (u_char *) "Host", 4) == 0) {
        return r->headers_in.server;
    }

    return empty;
}


static ngx_int_t
htaccess_expand_copy(ngx_pool_t *pool, u_char **dst, u_char *last,
    u_char *src, size_t len)
{
    if (*dst + len > last) {
        return NGX_ERROR;
    }

    *dst = ngx_cpymem(*dst, src, len);
    return NGX_OK;
}


ngx_int_t
htaccess_expand(ngx_pool_t *pool, ngx_http_request_t *r,
    htaccess_request_state_t *state, ngx_str_t *tpl, ngx_str_t *match,
    int *captures, ngx_uint_t ncaptures, ngx_str_t *out)
{
    u_char      *p, *end, *dst, *dlast, *ostart;
    size_t       len;
    ngx_str_t    name, value;
    ngx_uint_t   n;

    len = tpl->len * 4 + 256;
    dst = ngx_pnalloc(pool, len);
    if (dst == NULL) {
        return NGX_ERROR;
    }

    ostart = dst;
    dlast = dst + len;
    p = tpl->data;
    end = tpl->data + tpl->len;

    while (p < end) {
        if (*p == '\\' && p + 1 < end) {
            if (htaccess_expand_copy(pool, &dst, dlast, p + 1, 1) != NGX_OK) {
                return NGX_ERROR;
            }
            p += 2;
            continue;
        }

        if (*p == '$' && p + 1 < end && p[1] >= '0' && p[1] <= '9') {
            n = p[1] - '0';
            if (match != NULL && captures != NULL && n * 2 + 1 < ncaptures
                && captures[n * 2] >= 0)
            {
                if (htaccess_expand_copy(pool, &dst, dlast,
                        match->data + captures[n * 2],
                        captures[n * 2 + 1] - captures[n * 2]) != NGX_OK)
                {
                    return NGX_ERROR;
                }
            }
            p += 2;
            continue;
        }

        if (*p == '%' && p + 1 < end && p[1] == '{') {
            u_char  *name_start = p + 2;
            p = ngx_strlchr(name_start, end, '}');
            if (p == NULL) {
                return NGX_ERROR;
            }

            name.data = name_start;
            name.len = p - name_start;

            if (name.len >= 4 && ngx_strncasecmp(name.data, (u_char *) "ENV:", 4) == 0)
            {
                name.data += 4;
                name.len -= 4;
                value.data = NULL;
                value.len = 0;
                {
                    ngx_str_t *v = htaccess_env_get(state, &name);
                    if (v != NULL) {
                        value = *v;
                    }
                }
            } else if (name.len >= 9
                       && ngx_strncasecmp(name.data, (u_char *) "HTTP_HOST", 9) == 0)
            {
                value = r->headers_in.server;
            } else if (name.len >= 5
                       && ngx_strncasecmp(name.data, (u_char *) "HTTP:", 5) == 0)
            {
                name.data += 5;
                name.len -= 5;
                value = htaccess_get_http_header(r, &name);
            } else if (name.len >= 12
                       && ngx_strncasecmp(name.data, (u_char *) "QUERY_STRING", 12)
                          == 0)
            {
                value = state->query_string;
            } else if (name.len >= 16
                       && ngx_strncasecmp(name.data,
                           (u_char *) "REQUEST_FILENAME", 16) == 0)
            {
                value = state->filename;
            } else if (name.len >= 11
                       && ngx_strncasecmp(name.data, (u_char *) "REQUEST_URI", 11)
                          == 0)
            {
                value = state->uri;
            } else {
                p++;
                continue;
            }

            if (value.len && htaccess_expand_copy(pool, &dst, dlast,
                    value.data, value.len) != NGX_OK)
            {
                return NGX_ERROR;
            }

            p++;
            continue;
        }

        if (htaccess_expand_copy(pool, &dst, dlast, p++, 1) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    out->data = ostart;
    out->len = dst - ostart;
    return NGX_OK;
}

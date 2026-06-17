
#include "htaccess_directives.h"


static ngx_int_t
htaccess_compile_alias(ngx_pool_t *pool, htaccess_alias_redirect_t *alias)
{
    ngx_regex_compile_t  cc;

    if (alias->regex != NULL) {
        return NGX_OK;
    }

    ngx_memzero(&cc, sizeof(ngx_regex_compile_t));
    cc.pattern = alias->from;
    cc.pool = pool;
    cc.err.len = 0;
    cc.err.data = NULL;

    if (ngx_regex_compile(&cc) != NGX_OK) {
        return NGX_ERROR;
    }

    alias->regex = cc.regex;
    return NGX_OK;
}


static ngx_int_t
htaccess_alias_match(ngx_pool_t *pool, htaccess_alias_redirect_t *alias,
    ngx_str_t *uri, ngx_str_t *out, ngx_uint_t regex)
{
    ngx_int_t  rc;

    if (regex) {
        if (htaccess_compile_alias(pool, alias) != NGX_OK) {
            return NGX_ERROR;
        }

        rc = ngx_regex_exec(alias->regex, uri, NULL, 0);
        if (rc == NGX_REGEX_NO_MATCHED) {
            return NGX_DECLINED;
        }
        if (rc < 0) {
            return NGX_ERROR;
        }

        *out = alias->to;
        return NGX_OK;
    }

    if (uri->len < alias->from.len) {
        return NGX_DECLINED;
    }

    if (ngx_strncmp(uri->data, alias->from.data, alias->from.len) != 0) {
        return NGX_DECLINED;
    }

    if (uri->len == alias->from.len
        || uri->data[alias->from.len] == '/'
        || uri->data[alias->from.len] == '?')
    {
        *out = alias->to;
        return NGX_OK;
    }

    return NGX_DECLINED;
}


htaccess_result_t
htaccess_apply_alias(ngx_http_request_t *r, htaccess_merged_ctx_t *merged,
    htaccess_request_state_t *state)
{
    htaccess_dir_ctx_t    *dirs;
    htaccess_directive_t  *directives;
    htaccess_alias_redirect_t *alias;
    ngx_uint_t             i, j;
    ngx_str_t              out;
    ngx_int_t              rc;
    ngx_uint_t             regex;

    if (merged->dirs == NULL) {
        return HTACCESS_RESULT_DECLINED;
    }

    dirs = merged->dirs->elts;

    for (i = 0; i < merged->dirs->nelts; i++) {
        if (dirs[i].directives == NULL) {
            continue;
        }

        directives = dirs[i].directives->elts;
        for (j = 0; j < dirs[i].directives->nelts; j++) {
            switch (directives[j].id) {

            case HTACCESS_DIR_REDIRECT:
            case HTACCESS_DIR_REDIRECT_PERMANENT:
            case HTACCESS_DIR_REDIRECT_TEMP:
                regex = 0;
                alias = &directives[j].u.alias;
                break;

            case HTACCESS_DIR_REDIRECT_MATCH:
                regex = 1;
                alias = &directives[j].u.alias;
                break;

            default:
                continue;
            }

            rc = htaccess_alias_match(r->pool, alias, &state->uri, &out, regex);
            if (rc != NGX_OK) {
                continue;
            }

            state->uri = out;
            state->redirect_code = alias->permanent
                ? NGX_HTTP_MOVED_PERMANENTLY
                : NGX_HTTP_MOVED_TEMPORARILY;
            return HTACCESS_RESULT_REDIRECT;
        }
    }

    return HTACCESS_RESULT_DECLINED;
}

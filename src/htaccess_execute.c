
#include "htaccess_directives.h"


static void
htaccess_resolve_filename(ngx_http_request_t *r, htaccess_request_state_t *state)
{
    ngx_http_core_loc_conf_t  *clcf;
    u_char                    *name;
    size_t                     root_len, len;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    if (clcf->root.len == 0) {
        state->filename.len = 0;
        return;
    }

    root_len = clcf->root.len;
    len = root_len + state->uri.len + 1;

    name = ngx_pnalloc(r->pool, len);
    if (name == NULL) {
        return;
    }

    ngx_memcpy(name, clcf->root.data, root_len);
    if (clcf->root.data[root_len - 1] != '/') {
        name[root_len++] = '/';
    }

    if (state->uri.len > 1) {
        ngx_memcpy(name + root_len, state->uri.data + 1, state->uri.len - 1);
        root_len += state->uri.len - 1;
    }

    state->filename.data = name;
    state->filename.len = root_len;
}


static ngx_int_t
htaccess_send_redirect(ngx_http_request_t *r, htaccess_request_state_t *state)
{
    ngx_str_t         location;
    ngx_table_elt_t  *h;

    location = state->uri;

    if (location.len > 0 && location.data[0] != '/'
        && location.data[0] != 'h')
    {
        u_char  *p;
        p = ngx_pnalloc(r->pool, location.len + 1);
        if (p == NULL) {
            return NGX_ERROR;
        }
        p[0] = '/';
        ngx_memcpy(p + 1, location.data, location.len);
        location.data = p;
        location.len++;
    }

    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    h->hash = 1;
    h->key = (ngx_str_t) ngx_string("Location");
    h->value = location;
    r->headers_out.location = h;
    r->headers_out.status = state->redirect_code
        ? state->redirect_code : NGX_HTTP_MOVED_TEMPORARILY;
    r->headers_out.content_length_n = 0;

    return r->headers_out.status;
}


static ngx_int_t
htaccess_apply_uri(ngx_http_request_t *r, htaccess_request_state_t *state)
{
    size_t  len;

    len = state->uri.len;
    r->uri.len = len;
    r->uri.data = ngx_pnalloc(r->pool, len);
    if (r->uri.data == NULL) {
        return NGX_ERROR;
    }
    ngx_memcpy(r->uri.data, state->uri.data, len);

    if (r->args.len) {
        r->unparsed_uri.len = len + 1 + r->args.len;
        r->unparsed_uri.data = ngx_pnalloc(r->pool, r->unparsed_uri.len);
        if (r->unparsed_uri.data == NULL) {
            return NGX_ERROR;
        }
        ngx_snprintf(r->unparsed_uri.data, r->unparsed_uri.len, "%V?%V",
                     &r->uri, &r->args);
    } else {
        r->unparsed_uri = r->uri;
    }

    r->valid_unparsed_uri = 0;
    r->internal = 1;

    return NGX_OK;
}


static ngx_int_t
htaccess_error_uri(ngx_http_request_t *r, htaccess_merged_ctx_t *merged,
    ngx_uint_t code, ngx_str_t *uri)
{
    htaccess_error_document_t  *docs;
    ngx_uint_t                  i;

    if (merged->error_documents == NULL) {
        return NGX_DECLINED;
    }

    docs = merged->error_documents->elts;
    for (i = 0; i < merged->error_documents->nelts; i++) {
        if (docs[i].code == code) {
            *uri = docs[i].uri;
            return NGX_OK;
        }
    }

    return NGX_DECLINED;
}


ngx_int_t
htaccess_execute(ngx_http_request_t *r, ngx_http_htaccess_loc_conf_t *conf,
    htaccess_cache_t *cache)
{
    htaccess_merged_ctx_t      merged;
    htaccess_request_state_t   state;
    htaccess_result_t          result;
    ngx_uint_t                 pass;
    ngx_int_t                  rc;
    ngx_str_t                  request_uri;

    ngx_memzero(&state, sizeof(state));
    state.uri = r->uri;
    state.query_string = r->args;
    request_uri = r->uri;

    htaccess_resolve_filename(r, &state);

    for (pass = 0; pass < conf->max_redirects; pass++) {

        if (htaccess_build_merged_ctx(r, conf, cache, &merged) != NGX_OK) {
            return NGX_ERROR;
        }

        if (pass == 0) {
            result = htaccess_apply_files_deny(r, &merged, &request_uri);
            if (result == HTACCESS_RESULT_FORBIDDEN) {
                return NGX_HTTP_FORBIDDEN;
            }
        }

        result = htaccess_apply_alias(r, &merged, &state);
        if (result == HTACCESS_RESULT_REDIRECT) {
            return htaccess_send_redirect(r, &state);
        }

        result = htaccess_apply_rewrite(r, conf, &merged, &state);
        if (result == HTACCESS_RESULT_REDIRECT) {
            return htaccess_send_redirect(r, &state);
        }

        if (result == HTACCESS_RESULT_OK) {
            rc = htaccess_apply_uri(r, &state);
            if (rc != NGX_OK) {
                return rc;
            }
            htaccess_resolve_filename(r, &state);
            continue;
        }

        result = htaccess_apply_auth(r, &merged);
        if (result == HTACCESS_RESULT_UNAUTHORIZED) {
            {
                ngx_str_t  uri;
                if (htaccess_error_uri(r, &merged, NGX_HTTP_UNAUTHORIZED, &uri)
                    == NGX_OK)
                {
                    state.uri = uri;
                    if (htaccess_apply_uri(r, &state) == NGX_OK) {
                        continue;
                    }
                }
            }
            return NGX_HTTP_UNAUTHORIZED;
        }
        if (result == HTACCESS_RESULT_FORBIDDEN) {
            {
                ngx_str_t  uri;
                if (htaccess_error_uri(r, &merged, NGX_HTTP_FORBIDDEN, &uri)
                    == NGX_OK)
                {
                    state.uri = uri;
                    if (htaccess_apply_uri(r, &state) == NGX_OK) {
                        continue;
                    }
                }
            }
            return NGX_HTTP_FORBIDDEN;
        }

        htaccess_apply_headers(r, &merged);
        htaccess_apply_options(r, &merged);

        break;
    }

    return NGX_DECLINED;
}

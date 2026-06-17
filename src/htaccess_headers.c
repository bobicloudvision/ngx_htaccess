
#include "htaccess_directives.h"


static void
htaccess_set_header(ngx_http_request_t *r, ngx_str_t *name, ngx_str_t *value)
{
    ngx_table_elt_t  *h;

    h = ngx_list_push(&r->headers_out.headers);
    if (h == NULL) {
        return;
    }

    h->hash = 1;
    h->key = *name;
    h->value = *value;
}


htaccess_result_t
htaccess_apply_headers(ngx_http_request_t *r, htaccess_merged_ctx_t *merged)
{
    htaccess_header_t  *headers;
    ngx_uint_t          i;

    if (merged->headers) {
        headers = merged->headers->elts;
        for (i = 0; i < merged->headers->nelts; i++) {
            if (headers[i].name.len == 0) {
                continue;
            }

            if (headers[i].action.len >= 3
                && ngx_strncasecmp(headers[i].action.data, (u_char *) "set", 3)
                   == 0)
            {
                htaccess_set_header(r, &headers[i].name, &headers[i].value);
            }
        }
    }

    if (merged->expires_active && merged->expires_by_type) {
        headers = merged->expires_by_type->elts;
        for (i = 0; i < merged->expires_by_type->nelts; i++) {
            if (r->headers_out.content_type.len
                && headers[i].name.len
                && r->headers_out.content_type.len >= headers[i].name.len
                && ngx_strncmp(r->headers_out.content_type.data,
                    headers[i].name.data, headers[i].name.len) == 0)
            {
                htaccess_set_header(r, &(ngx_str_t) ngx_string("Expires"),
                    &headers[i].value);
            }
        }
    }

    return HTACCESS_RESULT_DECLINED;
}


htaccess_result_t
htaccess_apply_options(ngx_http_request_t *r, htaccess_merged_ctx_t *merged)
{
    if ((merged->options & 1) && r->uri.len > 1
        && r->uri.data[r->uri.len - 1] == '/')
    {
        ngx_http_core_loc_conf_t  *clcf;
        ngx_file_info_t            fi;
        u_char                    *name;
        size_t                     len;

        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
        len = clcf->root.len + r->uri.len + 1;
        name = ngx_pnalloc(r->pool, len);
        if (name == NULL) {
            return HTACCESS_RESULT_ERROR;
        }

        ngx_snprintf(name, len, "%V%V", &clcf->root, &r->uri);

        if (ngx_file_info(name, &fi) != NGX_FILE_ERROR && ngx_is_dir(&fi)) {
            return HTACCESS_RESULT_FORBIDDEN;
        }
    }

    return HTACCESS_RESULT_DECLINED;
}

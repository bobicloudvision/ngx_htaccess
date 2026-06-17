
#include "htaccess_directives.h"
#include <crypt.h>


static ngx_int_t
htaccess_check_htpasswd(ngx_pool_t *pool, ngx_str_t *file, ngx_str_t *user,
    ngx_str_t *pass)
{
    ngx_fd_t      fd;
    ngx_file_info_t fi;
    u_char       *buf, *p, *last, *colon;
    ssize_t       n;
    ngx_str_t     u, pw;

    fd = ngx_open_file(file->data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
    if (fd == NGX_INVALID_FILE) {
        return NGX_DECLINED;
    }

    if (ngx_fd_info(fd, &fi) == NGX_FILE_ERROR) {
        ngx_close_file(fd);
        return NGX_ERROR;
    }

    buf = ngx_alloc(fi.st_size + 1, ngx_cycle->log);
    if (buf == NULL) {
        ngx_close_file(fd);
        return NGX_ERROR;
    }

    n = ngx_read_fd(fd, buf, fi.st_size);
    ngx_close_file(fd);

    if (n == NGX_FILE_ERROR) {
        ngx_free(buf);
        return NGX_ERROR;
    }

    p = buf;
    last = buf + n;

    while (p < last) {
        colon = ngx_strlchr(p, last, ':');
        if (colon == NULL) {
            break;
        }

        u.data = p;
        u.len = colon - p;

        p = colon + 1;
        colon = ngx_strlchr(p, last, '\n');
        if (colon == NULL) {
            colon = last;
        }

        pw.data = p;
        pw.len = colon - p;
        while (pw.len > 0 && (pw.data[pw.len - 1] == '\r'
                              || pw.data[pw.len - 1] == '\n'))
        {
            pw.len--;
        }

        if (u.len == user->len
            && ngx_strncmp(u.data, user->data, user->len) == 0)
        {
            if (pw.len > 0 && pw.data[0] == '$') {
                char        *crypted;
                u_char       plain[256];

                if (pass->len >= sizeof(plain)) {
                    ngx_free(buf);
                    return NGX_DECLINED;
                }

                ngx_memcpy(plain, pass->data, pass->len);
                plain[pass->len] = '\0';

                crypted = crypt((char *) plain, (char *) pw.data);
                if (crypted != NULL
                    && ngx_strlen(crypted) == pw.len
                    && ngx_strncmp((u_char *) crypted, pw.data, pw.len) == 0)
                {
                    ngx_free(buf);
                    return NGX_OK;
                }
            }

            if (pw.len == pass->len
                && ngx_strncmp(pw.data, pass->data, pass->len) == 0)
            {
                ngx_free(buf);
                return NGX_OK;
            }
        }

        p = colon;
        while (p < last && (*p == '\n' || *p == '\r')) {
            p++;
        }
    }

    ngx_free(buf);
    return NGX_DECLINED;
}


static ngx_int_t
htaccess_require_satisfied(ngx_http_request_t *r, htaccess_merged_ctx_t *merged)
{
    ngx_str_t   *reqs, *allow, *deny;
    ngx_uint_t   i, j;
    ngx_int_t    denied, allowed;

    if (merged->requires == NULL || merged->requires->nelts == 0) {
        return NGX_DECLINED;
    }

    reqs = merged->requires->elts;

    for (i = 0; i < merged->requires->nelts; i++) {
        if (reqs[i].len >= 11
            && ngx_strncasecmp(reqs[i].data, (u_char *) "all granted", 11) == 0)
        {
            return NGX_OK;
        }

        if (reqs[i].len >= 10
            && ngx_strncasecmp(reqs[i].data, (u_char *) "all denied", 10) == 0)
        {
            return NGX_HTTP_FORBIDDEN;
        }

        if (reqs[i].len >= 11
            && ngx_strncasecmp(reqs[i].data, (u_char *) "valid-user", 10) == 0)
        {
            if (r->headers_in.user.len == 0) {
                return NGX_HTTP_UNAUTHORIZED;
            }

            if (merged->auth_user_file.len == 0) {
                return NGX_HTTP_UNAUTHORIZED;
            }

            if (htaccess_check_htpasswd(r->pool, &merged->auth_user_file,
                    &r->headers_in.user, &r->headers_in.passwd) != NGX_OK)
            {
                return NGX_HTTP_UNAUTHORIZED;
            }

            return NGX_OK;
        }
    }

    if (merged->order.len >= 4
        && ngx_strncasecmp(merged->order.data, (u_char *) "deny", 4) == 0)
    {
        denied = 0;
        allowed = 0;

        if (merged->denies) {
            deny = merged->denies->elts;
            for (j = 0; j < merged->denies->nelts; j++) {
                if (deny[j].len == 3
                    && ngx_strncmp(deny[j].data, "all", 3) == 0)
                {
                    denied = 1;
                }
            }
        }

        if (merged->allows) {
            allow = merged->allows->elts;
            for (j = 0; j < merged->allows->nelts; j++) {
                if (allow[j].len == 3
                    && ngx_strncmp(allow[j].data, "all", 3) == 0)
                {
                    allowed = 1;
                }
            }
        }

        if (denied && !allowed) {
            return NGX_HTTP_FORBIDDEN;
        }
    }

    return NGX_DECLINED;
}


htaccess_result_t
htaccess_apply_auth(ngx_http_request_t *r, htaccess_merged_ctx_t *merged)
{
    ngx_int_t  rc;

    if (merged->auth_type.len == 0 && merged->requires == NULL
        && merged->allows == NULL && merged->denies == NULL)
    {
        return HTACCESS_RESULT_DECLINED;
    }

    rc = htaccess_require_satisfied(r, merged);

    if (rc == NGX_OK) {
        return HTACCESS_RESULT_DECLINED;
    }

    if (rc == NGX_HTTP_UNAUTHORIZED) {
        if (merged->auth_name.len) {
            r->headers_out.www_authenticate = ngx_list_push(&r->headers_out.headers);
            if (r->headers_out.www_authenticate) {
                r->headers_out.www_authenticate->hash = 1;
                r->headers_out.www_authenticate->key.data = (u_char *) "WWW-Authenticate";
                r->headers_out.www_authenticate->key.len = sizeof("WWW-Authenticate") - 1;
                r->headers_out.www_authenticate->value.len = merged->auth_name.len + 7;
                r->headers_out.www_authenticate->value.data = ngx_pnalloc(r->pool,
                    r->headers_out.www_authenticate->value.len);
                if (r->headers_out.www_authenticate->value.data) {
                    ngx_snprintf(r->headers_out.www_authenticate->value.data,
                        r->headers_out.www_authenticate->value.len,
                        "Basic realm=\"%V\"", &merged->auth_name);
                }
            }
        }
        return HTACCESS_RESULT_UNAUTHORIZED;
    }

    if (rc == NGX_HTTP_FORBIDDEN) {
        return HTACCESS_RESULT_FORBIDDEN;
    }

    return HTACCESS_RESULT_DECLINED;
}

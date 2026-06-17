
#include "htaccess_directives.h"


static ngx_int_t
htaccess_directive_id(ngx_str_t *name)
{
    static struct {
        char                   *name;
        htaccess_directive_id_t id;
    } map[] = {
        { "RewriteEngine", HTACCESS_DIR_REWRITE_ENGINE },
        { "RewriteBase", HTACCESS_DIR_REWRITE_BASE },
        { "RewriteCond", HTACCESS_DIR_REWRITE_COND },
        { "RewriteRule", HTACCESS_DIR_REWRITE_RULE },
        { "Redirect", HTACCESS_DIR_REDIRECT },
        { "RedirectMatch", HTACCESS_DIR_REDIRECT_MATCH },
        { "RedirectPermanent", HTACCESS_DIR_REDIRECT_PERMANENT },
        { "RedirectTemp", HTACCESS_DIR_REDIRECT_TEMP },
        { "AuthType", HTACCESS_DIR_AUTH_TYPE },
        { "AuthName", HTACCESS_DIR_AUTH_NAME },
        { "AuthUserFile", HTACCESS_DIR_AUTH_USER_FILE },
        { "Require", HTACCESS_DIR_REQUIRE },
        { "Order", HTACCESS_DIR_ORDER },
        { "Allow", HTACCESS_DIR_ALLOW },
        { "Deny", HTACCESS_DIR_DENY },
        { "Header", HTACCESS_DIR_HEADER },
        { "ExpiresActive", HTACCESS_DIR_EXPIRES_ACTIVE },
        { "ExpiresByType", HTACCESS_DIR_EXPIRES_BY_TYPE },
        { "ExpiresDefault", HTACCESS_DIR_EXPIRES_DEFAULT },
        { "ErrorDocument", HTACCESS_DIR_ERROR_DOCUMENT },
        { "Options", HTACCESS_DIR_OPTIONS },
        { "DirectoryIndex", HTACCESS_DIR_DIRECTORY_INDEX },
        { NULL, 0 }
    };

    ngx_uint_t  i;

    for (i = 0; map[i].name; i++) {
        if (name->len == ngx_strlen(map[i].name)
            && ngx_strncasecmp(name->data, (u_char *) map[i].name, name->len)
               == 0)
        {
            return map[i].id;
        }
    }

    return -1;
}


static ngx_int_t
htaccess_join_args(ngx_pool_t *pool, ngx_array_t *args, ngx_uint_t from,
    ngx_str_t *out)
{
    ngx_str_t  *a;
    ngx_uint_t  i;
    size_t      len;
    u_char     *p;

    if (args->nelts <= from) {
        out->len = 0;
        out->data = NULL;
        return NGX_ERROR;
    }

    a = args->elts;
    len = 0;
    for (i = from; i < args->nelts; i++) {
        len += a[i].len + (i > from ? 1 : 0);
    }

    p = ngx_pnalloc(pool, len);
    if (p == NULL) {
        return NGX_ERROR;
    }

    out->data = p;
    out->len = len;
    for (i = from; i < args->nelts; i++) {
        if (i > from) {
            *p++ = ' ';
        }
        p = ngx_cpymem(p, a[i].data, a[i].len);
    }

    return NGX_OK;
}


static ngx_int_t
htaccess_parse_rewrite_cond(ngx_pool_t *pool, ngx_array_t *args,
    htaccess_rewrite_cond_t *cond)
{
    ngx_str_t  *test, *value;
    u_char     *p, *last;

    if (args->nelts < 2) {
        return NGX_ERROR;
    }

    test = ((ngx_str_t *) args->elts) + 1;
    value = args->nelts > 2 ? ((ngx_str_t *) args->elts) + 2 : NULL;

    ngx_memzero(cond, sizeof(htaccess_rewrite_cond_t));

    if (test->len >= 3 && test->data[0] == '%' && test->data[1] == '{') {
        p = test->data + 2;
        last = ngx_strlchr(p, test->data + test->len, '}');
        if (last == NULL) {
            return NGX_ERROR;
        }

        if (last - p >= 17
            && ngx_strncasecmp(p, (u_char *) "REQUEST_FILENAME", 16) == 0)
        {
            cond->test = HTACCESS_COND_TEST_FILENAME;
            p += 16;
        } else if (last - p >= 12
                   && ngx_strncasecmp(p, (u_char *) "REQUEST_URI", 11) == 0)
        {
            cond->test = HTACCESS_COND_TEST_URI;
            p += 11;
        } else if (last - p >= 13
                   && ngx_strncasecmp(p, (u_char *) "QUERY_STRING", 12) == 0)
        {
            cond->test = HTACCESS_COND_TEST_QUERY_STRING;
            p += 12;
        } else if (last - p >= 9
                   && ngx_strncasecmp(p, (u_char *) "HTTP_HOST", 9) == 0)
        {
            cond->test = HTACCESS_COND_TEST_HTTP;
            cond->name.data = (u_char *) "Host";
            cond->name.len = 4;
        } else if (last - p >= 5
                   && ngx_strncasecmp(p, (u_char *) "HTTP:", 5) == 0)
        {
            cond->test = HTACCESS_COND_TEST_HTTP;
            p += 5;
            cond->name.len = last - p;
            cond->name.data = p;
        } else if (last - p >= 4
                   && ngx_strncasecmp(p, (u_char *) "ENV:", 4) == 0)
        {
            cond->test = HTACCESS_COND_TEST_ENV;
            p += 4;
            cond->name.len = last - p;
            cond->name.data = p;
        } else {
            cond->test = HTACCESS_COND_TEST_OTHER;
        }
    } else if (test->len >= 1 && test->data[0] == '%') {
        cond->test = HTACCESS_COND_TEST_FSPATH;
        cond->name.data = test->data + 1;
        cond->name.len = test->len - 1;
    }

    if (value == NULL) {
        return NGX_OK;
    }

    cond->value = *value;

    if (value->len == 2 && value->data[0] == '-' && value->data[1] == 'f') {
        cond->op = HTACCESS_COND_OP_FILE;
    } else if (value->len == 3 && ngx_strncmp(value->data, "-d", 2) == 0) {
        cond->op = HTACCESS_COND_OP_DIR;
    } else if (value->len == 3 && ngx_strncmp(value->data, "-l", 2) == 0) {
        cond->op = HTACCESS_COND_OP_SYMLINK;
    } else if (value->len == 3 && ngx_strncmp(value->data, "-s", 2) == 0) {
        cond->op = HTACCESS_COND_OP_EXISTS;
    } else if (value->len == 4 && ngx_strncmp(value->data, "!-f", 3) == 0) {
        cond->op = HTACCESS_COND_OP_NOTFILE;
    } else if (value->len == 4 && ngx_strncmp(value->data, "!-d", 3) == 0) {
        cond->op = HTACCESS_COND_OP_NOTDIR;
    } else if (value->len == 4 && ngx_strncmp(value->data, "!-l", 3) == 0) {
        cond->op = HTACCESS_COND_OP_NOTSYMLINK;
    } else if (value->len == 4 && ngx_strncmp(value->data, "!-s", 3) == 0) {
        cond->op = HTACCESS_COND_OP_NOTEXISTS;
    } else {
        cond->op = HTACCESS_COND_OP_REGEX;
    }

    if (args->nelts > 3) {
        ngx_str_t *flags = ((ngx_str_t *) args->elts) + 3;
        ngx_str_t  stripped;

        stripped = *flags;
        while (stripped.len > 0 && stripped.data[0] == ' ') {
            stripped.data++;
            stripped.len--;
        }
        if (stripped.len >= 2 && stripped.data[0] == '['
            && stripped.data[stripped.len - 1] == ']')
        {
            stripped.data++;
            stripped.len -= 2;
        }

        if (stripped.len >= 2
            && ngx_strncasecmp(stripped.data, (u_char *) "OR", 2) == 0)
        {
            cond->or_next = 1;
        }
    }

    return NGX_OK;
}


static void
htaccess_parse_rule_flags(ngx_pool_t *pool, ngx_str_t *flags,
    htaccess_rewrite_rule_t *rule)
{
    u_char  *p, *last, *eq;
    ngx_str_t part;

    if (flags->len == 0) {
        return;
    }

    p = flags->data;
    last = flags->data + flags->len;

    while (p < last) {
        while (p < last && (*p == ',' || *p == ' ')) {
            p++;
        }
        if (p >= last) {
            break;
        }

        part.data = p;
        while (p < last && *p != ',') {
            p++;
        }
        part.len = p - part.data;

        while (part.len > 0 && (part.data[0] == ' ' || part.data[0] == '[')) {
            part.data++;
            part.len--;
        }
        while (part.len > 0
               && (part.data[part.len - 1] == ' ' || part.data[part.len - 1] == ']'))
        {
            part.len--;
        }

        if (part.len == 0) {
            continue;
        }

        if (part.len == 1 && part.data[0] == 'L') {
            rule->last = 1;
        } else if (part.len == 3 && ngx_strncasecmp(part.data, (u_char *) "END", 3) == 0) {
            rule->end = 1;
        } else if (part.len == 2 && ngx_strncasecmp(part.data, (u_char *) "NC", 2) == 0) {
            rule->nocase = 1;
        } else if (part.len >= 2 && ngx_strncasecmp(part.data, (u_char *) "R", 1) == 0) {
            rule->redirect = 1;
            rule->redirect_code = NGX_HTTP_MOVED_TEMPORARILY;
            eq = ngx_strlchr(part.data, part.data + part.len, '=');
            if (eq != NULL) {
                ngx_int_t  code;

                code = ngx_atoi(eq + 1, part.data + part.len - eq - 1);
                rule->redirect_code = (code == NGX_ERROR)
                    ? NGX_HTTP_MOVED_TEMPORARILY
                    : (ngx_uint_t) code;
            }
        } else if (part.len == 1 && part.data[0] == 'C') {
            rule->chain = 1;
        } else if (part.len == 3 && ngx_strncasecmp(part.data, (u_char *) "QSA", 3) == 0) {
            rule->qsa = 1;
        } else if (part.len >= 2 && ngx_strncasecmp(part.data, (u_char *) "E=", 2) == 0) {
            ngx_str_t *env;

            if (rule->env_vars == NULL) {
                rule->env_vars = ngx_array_create(pool, 2, sizeof(ngx_str_t));
            }
            env = ngx_array_push(rule->env_vars);
            if (env != NULL) {
                env->len = part.len - 2;
                env->data = part.data + 2;
            }
        }
    }
}


static ngx_int_t
htaccess_push_directive(ngx_pool_t *pool, htaccess_parsed_file_t *file,
    htaccess_directive_id_t id, ngx_array_t *args, ngx_array_t **pending_conds,
    ngx_str_t *files_match)
{
    htaccess_directive_t  *d;
    ngx_str_t             *name, *arg;
    ngx_uint_t             offset;

    if (id == HTACCESS_DIR_REWRITE_COND) {
        htaccess_rewrite_cond_t  *pc;

        if (*pending_conds == NULL) {
            *pending_conds = ngx_array_create(pool, 4,
                                              sizeof(htaccess_rewrite_cond_t));
            if (*pending_conds == NULL) {
                return NGX_ERROR;
            }
        }

        pc = ngx_array_push(*pending_conds);
        if (pc == NULL) {
            return NGX_ERROR;
        }

        return htaccess_parse_rewrite_cond(pool, args, pc);
    }

    d = ngx_array_push(file->directives);
    if (d == NULL) {
        return NGX_ERROR;
    }

    ngx_memzero(d, sizeof(htaccess_directive_t));
    d->id = id;
    if (files_match != NULL && files_match->len > 0) {
        d->files_match = *files_match;
    }

    switch (id) {

    case HTACCESS_DIR_REWRITE_ENGINE:
        arg = ((ngx_str_t *) args->elts) + 1;
        d->u.flag = (arg->len == 2 && ngx_strncasecmp(arg->data, (u_char *) "on", 2) == 0);
        break;

    case HTACCESS_DIR_REWRITE_BASE:
        d->u.str = ((ngx_str_t *) args->elts)[1];
        break;

    case HTACCESS_DIR_REWRITE_RULE:
        if (args->nelts < 3) {
            return NGX_ERROR;
        }
        d->u.rewrite_rule.pattern = ((ngx_str_t *) args->elts)[1];
        d->u.rewrite_rule.substitution = ((ngx_str_t *) args->elts)[2];
        if (args->nelts > 3) {
            htaccess_parse_rule_flags(pool, &((ngx_str_t *) args->elts)[3],
                &d->u.rewrite_rule);
        }
        if (*pending_conds != NULL && (*pending_conds)->nelts > 0) {
            d->u.rewrite_rule.conditions = *pending_conds;
            *pending_conds = NULL;
        }
        break;

    case HTACCESS_DIR_REDIRECT:
    case HTACCESS_DIR_REDIRECT_PERMANENT:
    case HTACCESS_DIR_REDIRECT_TEMP:
        if (args->nelts < 3) {
            return NGX_ERROR;
        }
        d->u.alias.from = ((ngx_str_t *) args->elts)[1];
        d->u.alias.to = ((ngx_str_t *) args->elts)[2];
        d->u.alias.permanent = (id == HTACCESS_DIR_REDIRECT_PERMANENT)
            || (id == HTACCESS_DIR_REDIRECT
                && args->nelts > 3
                && ((ngx_str_t *) args->elts)[3].len >= 3
                && ngx_strncmp(((ngx_str_t *) args->elts)[3].data, "301", 3) == 0);
        break;

    case HTACCESS_DIR_REDIRECT_MATCH:
        if (args->nelts < 3) {
            return NGX_ERROR;
        }
        d->u.alias.from = ((ngx_str_t *) args->elts)[1];
        d->u.alias.to = ((ngx_str_t *) args->elts)[2];
        d->u.alias.permanent = 1;
        break;

    case HTACCESS_DIR_AUTH_TYPE:
    case HTACCESS_DIR_AUTH_NAME:
    case HTACCESS_DIR_AUTH_USER_FILE:
    case HTACCESS_DIR_REQUIRE:
    case HTACCESS_DIR_ORDER:
    case HTACCESS_DIR_ALLOW:
    case HTACCESS_DIR_DENY:
    case HTACCESS_DIR_EXPIRES_DEFAULT:
    case HTACCESS_DIR_DIRECTORY_INDEX:
        if (id == HTACCESS_DIR_REQUIRE && args->nelts > 2) {
            return htaccess_join_args(pool, args, 1, &d->u.str);
        }
        d->u.str = ((ngx_str_t *) args->elts)[1];
        break;

    case HTACCESS_DIR_EXPIRES_ACTIVE:
        arg = ((ngx_str_t *) args->elts) + 1;
        d->u.flag = (arg->len == 2 && ngx_strncasecmp(arg->data, (u_char *) "on", 2) == 0);
        break;

    case HTACCESS_DIR_HEADER:
        if (args->nelts < 3) {
            return NGX_ERROR;
        }
        offset = 1;
        if (((ngx_str_t *) args->elts)[1].len == 6
            && ngx_strncasecmp(((ngx_str_t *) args->elts)[1].data,
                (u_char *) "always", 6) == 0)
        {
            d->u.header.always = 1;
            offset = 2;
        }
        if (args->nelts <= offset + 1) {
            return NGX_ERROR;
        }
        d->u.header.action = ((ngx_str_t *) args->elts)[offset];
        d->u.header.name = ((ngx_str_t *) args->elts)[offset + 1];
        if (args->nelts > offset + 2) {
            d->u.header.value = ((ngx_str_t *) args->elts)[offset + 2];
        }
        break;

    case HTACCESS_DIR_EXPIRES_BY_TYPE:
        if (args->nelts < 3) {
            return NGX_ERROR;
        }
        d->u.header.name = ((ngx_str_t *) args->elts)[1];
        d->u.header.value = ((ngx_str_t *) args->elts)[2];
        break;

    case HTACCESS_DIR_ERROR_DOCUMENT:
        if (args->nelts < 3) {
            return NGX_ERROR;
        }
        d->u.error_doc.code = ngx_atoi(((ngx_str_t *) args->elts)[1].data,
            ((ngx_str_t *) args->elts)[1].len);
        d->u.error_doc.uri = ((ngx_str_t *) args->elts)[2];
        break;

    case HTACCESS_DIR_OPTIONS:
        name = ((ngx_str_t *) args->elts) + 1;
        if (name->len >= 8 && ngx_strncasecmp(name->data, (u_char *) "-Indexes", 8) == 0) {
            d->u.flag = 1;
        }
        break;

    default:
        break;
    }

    return NGX_OK;
}


static ngx_uint_t
htaccess_block_enabled(ngx_str_t *block, ngx_uint_t in_rewrite,
    ngx_uint_t in_files)
{
    u_char  *p, *last;

    if (block->len < 2 || block->data[0] != '<') {
        return 1;
    }

    p = block->data + 1;
    last = block->data + block->len;

    if (ngx_strncasecmp(p, (u_char *) "IfModule", 8) == 0) {
        p += 8;
        while (p < last && (*p == ' ' || *p == '\t')) {
            p++;
        }
        if (last - p >= 14
            && ngx_strncasecmp(p, (u_char *) "mod_rewrite.c", 13) == 0)
        {
            return in_rewrite;
        }
        if (last - p >= 12
            && ngx_strncasecmp(p, (u_char *) "mod_alias.c", 11) == 0)
        {
            return 1;
        }
        if (last - p >= 11
            && ngx_strncasecmp(p, (u_char *) "mod_auth", 8) == 0)
        {
            return 1;
        }
        if (last - p >= 14
            && ngx_strncasecmp(p, (u_char *) "mod_headers.c", 13) == 0)
        {
            return 1;
        }
        if (last - p >= 14
            && ngx_strncasecmp(p, (u_char *) "mod_expires.c", 13) == 0)
        {
            return 1;
        }
        return 1;
    }

    if (ngx_strncasecmp(p, (u_char *) "Files", 5) == 0
        || ngx_strncasecmp(p, (u_char *) "FilesMatch", 10) == 0)
    {
        return in_files;
    }

    return 1;
}


static void
htaccess_parse_files_match(ngx_str_t *block, ngx_str_t *pattern)
{
    u_char  *p, *last, *start;

    ngx_memzero(pattern, sizeof(ngx_str_t));

    if (block->len < 12) {
        return;
    }

    p = block->data + 1;
    last = block->data + block->len - 1;

    if (ngx_strncasecmp(p, (u_char *) "FilesMatch", 10) != 0) {
        return;
    }

    p += 10;
    while (p < last && (*p == ' ' || *p == '\t')) {
        p++;
    }

    if (p >= last || *p != '"') {
        return;
    }

    start = ++p;
    while (p < last && *p != '"') {
        p++;
    }

    pattern->data = start;
    pattern->len = p - start;
}


htaccess_parsed_file_t *
htaccess_parse(ngx_pool_t *pool, u_char *start, u_char *end)
{
    u_char                 *p;
    ngx_array_t            *tokens;
    ngx_str_t              *t;
    htaccess_parsed_file_t *file;
    ngx_int_t               id;
    ngx_uint_t              depth;
    ngx_uint_t              enabled[16];
    ngx_str_t               files_match[16];
    ngx_array_t            *pending_conds;
    ngx_int_t               rc;

    file = ngx_pcalloc(pool, sizeof(htaccess_parsed_file_t));
    if (file == NULL) {
        return NULL;
    }

    file->pool = pool;
    file->directives = ngx_array_create(pool, 16, sizeof(htaccess_directive_t));
    if (file->directives == NULL) {
        return NULL;
    }

    p = start;
    depth = 0;
    pending_conds = NULL;
    ngx_memzero(enabled, sizeof(enabled));
    ngx_memzero(files_match, sizeof(files_match));
    enabled[0] = 1;

    while (p < end) {
        tokens = ngx_array_create(pool, 8, sizeof(ngx_str_t));
        if (tokens == NULL) {
            return NULL;
        }

        rc = htaccess_lex_line(&p, end, tokens, pool);
        if (rc != NGX_OK) {
            return NULL;
        }

        if (tokens->nelts == 0) {
            continue;
        }

        t = tokens->elts;

        if (t[0].len >= 2 && t[0].data[0] == '<'
            && t[0].data[t[0].len - 1] == '>')
        {
            if (t[0].len >= 3 && t[0].data[1] == '/') {
                if (depth > 0) {
                    depth--;
                }
                continue;
            }

            if (depth < 15) {
                depth++;
                enabled[depth] = htaccess_block_enabled(&t[0], 1, 1)
                    && enabled[depth - 1];
                htaccess_parse_files_match(&t[0], &files_match[depth]);
            }
            continue;
        }

        if (!enabled[depth]) {
            continue;
        }

        id = htaccess_directive_id(&t[0]);
        if (id < 0) {
            continue;
        }

        if (htaccess_push_directive(pool, file,
                (htaccess_directive_id_t) id, tokens, &pending_conds,
                &files_match[depth])
            != NGX_OK)
        {
            return NULL;
        }
    }

    return file;
}


ngx_int_t
htaccess_parse_file(ngx_pool_t *pool, ngx_str_t *path,
    htaccess_parsed_file_t **parsed)
{
    ngx_fd_t      fd;
    ngx_file_info_t fi;
    ssize_t       n;
    u_char       *buf;

    fd = ngx_open_file(path->data, NGX_FILE_RDONLY, NGX_FILE_OPEN, 0);
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

    if (n == NGX_FILE_ERROR || n != fi.st_size) {
        ngx_free(buf);
        return NGX_ERROR;
    }

    buf[n] = '\0';

    *parsed = htaccess_parse(pool, buf, buf + n);
    ngx_free(buf);

    return *parsed ? NGX_OK : NGX_ERROR;
}

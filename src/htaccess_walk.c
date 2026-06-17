
#include "htaccess_directives.h"


static ngx_int_t
htaccess_load_file(ngx_http_request_t *r, htaccess_cache_t *cache,
    ngx_http_htaccess_loc_conf_t *conf, ngx_str_t *path,
    htaccess_parsed_file_t **parsed)
{
    htaccess_cache_entry_t  *entry;
    ngx_pool_t              *pool;
    ngx_file_info_t          fi;
    ngx_int_t                 rc;

    *parsed = NULL;

    if (conf->cache_enable) {
        entry = htaccess_cache_lookup(cache, r->pool, path, 1);
        if (entry != NULL) {
            if (entry->absent) {
                return NGX_DECLINED;
            }
            *parsed = entry->parsed;
            return NGX_OK;
        }
    }

    if (ngx_file_info(path->data, &fi) == NGX_FILE_ERROR) {
        if (conf->cache_enable) {
            htaccess_cache_store(cache, r->pool, path, 0, 0, 1, NULL);
        }
        return NGX_DECLINED;
    }

    pool = ngx_create_pool(4096, r->connection->log);
    if (pool == NULL) {
        return NGX_ERROR;
    }

    rc = htaccess_parse_file(pool, path, parsed);
    if (rc != NGX_OK) {
        ngx_destroy_pool(pool);
        if (rc == NGX_DECLINED && conf->cache_enable) {
            htaccess_cache_store(cache, r->pool, path, fi.st_mtime, fi.st_size,
                                 1, NULL);
        }
        return rc;
    }

    if (conf->cache_enable) {
        htaccess_cache_store(cache, r->pool, path, fi.st_mtime, fi.st_size, 0,
                             *parsed);
    }

    return NGX_OK;
}


static void
htaccess_mark_rewrite_chains(ngx_array_t *directives)
{
    htaccess_directive_t  *d;
    ngx_uint_t             i, j;

    if (directives == NULL) {
        return;
    }

    d = directives->elts;
    for (i = 0; i < directives->nelts; i++) {
        if (d[i].id != HTACCESS_DIR_REWRITE_RULE || !d[i].u.rewrite_rule.chain) {
            continue;
        }

        for (j = i + 1; j < directives->nelts; j++) {
            if (d[j].id == HTACCESS_DIR_REWRITE_RULE) {
                d[j].u.rewrite_rule.chain_in = 1;
                break;
            }
        }
    }
}


static void
htaccess_merge_directive(ngx_pool_t *pool, htaccess_merged_ctx_t *merged,
    htaccess_dir_ctx_t *dir, htaccess_directive_t *d)
{
    ngx_str_t  *s;
    htaccess_files_rule_t *fr;

    switch (d->id) {

    case HTACCESS_DIR_REWRITE_ENGINE:
        dir->rewrite_engine = d->u.flag;
        break;

    case HTACCESS_DIR_REWRITE_BASE:
        dir->rewrite_base = d->u.str;
        break;

    case HTACCESS_DIR_REWRITE_RULE:
        if (dir->directives == NULL) {
            dir->directives = ngx_array_create(pool, 8,
                                               sizeof(htaccess_directive_t));
        }
        if (dir->directives != NULL) {
            htaccess_directive_t *dst = ngx_array_push(dir->directives);
            if (dst != NULL) {
                *dst = *d;
            }
            htaccess_mark_rewrite_chains(dir->directives);
        }
        break;

    case HTACCESS_DIR_REDIRECT:
    case HTACCESS_DIR_REDIRECT_MATCH:
    case HTACCESS_DIR_REDIRECT_PERMANENT:
    case HTACCESS_DIR_REDIRECT_TEMP:
        if (dir->directives == NULL) {
            dir->directives = ngx_array_create(pool, 4,
                                               sizeof(htaccess_directive_t));
        }
        if (dir->directives != NULL) {
            htaccess_directive_t *dst = ngx_array_push(dir->directives);
            if (dst != NULL) {
                *dst = *d;
            }
        }
        break;

    case HTACCESS_DIR_AUTH_TYPE:
        merged->auth_type = d->u.str;
        break;

    case HTACCESS_DIR_AUTH_NAME:
        merged->auth_name = d->u.str;
        break;

    case HTACCESS_DIR_AUTH_USER_FILE:
        merged->auth_user_file = d->u.str;
        break;

    case HTACCESS_DIR_REQUIRE:
        if (d->files_match.len > 0) {
            if (merged->files_rules == NULL) {
                merged->files_rules = ngx_array_create(pool, 2,
                    sizeof(htaccess_files_rule_t));
            }
            if (merged->files_rules != NULL) {
                fr = ngx_array_push(merged->files_rules);
                if (fr != NULL) {
                    ngx_memzero(fr, sizeof(htaccess_files_rule_t));
                    fr->pattern = d->files_match;
                    if (d->u.str.len >= 10
                        && ngx_strncasecmp(d->u.str.data, (u_char *) "all denied",
                            10) == 0)
                    {
                        fr->deny = 1;
                    }
                }
            }
            break;
        }
        if (merged->requires == NULL) {
            merged->requires = ngx_array_create(pool, 2, sizeof(ngx_str_t));
        }
        if (merged->requires != NULL) {
            s = ngx_array_push(merged->requires);
            if (s != NULL) {
                *s = d->u.str;
            }
        }
        break;

    case HTACCESS_DIR_ORDER:
        merged->order = d->u.str;
        break;

    case HTACCESS_DIR_ALLOW:
        if (merged->allows == NULL) {
            merged->allows = ngx_array_create(pool, 2, sizeof(ngx_str_t));
        }
        if (merged->allows != NULL) {
            s = ngx_array_push(merged->allows);
            if (s != NULL) {
                *s = d->u.str;
            }
        }
        break;

    case HTACCESS_DIR_DENY:
        if (merged->denies == NULL) {
            merged->denies = ngx_array_create(pool, 2, sizeof(ngx_str_t));
        }
        if (merged->denies != NULL) {
            s = ngx_array_push(merged->denies);
            if (s != NULL) {
                *s = d->u.str;
            }
        }
        break;

    case HTACCESS_DIR_HEADER:
        if (merged->headers == NULL) {
            merged->headers = ngx_array_create(pool, 4,
                                               sizeof(htaccess_header_t));
        }
        if (merged->headers != NULL) {
            htaccess_header_t *h = ngx_array_push(merged->headers);
            if (h != NULL) {
                *h = d->u.header;
            }
        }
        break;

    case HTACCESS_DIR_EXPIRES_ACTIVE:
        merged->expires_active = d->u.flag;
        break;

    case HTACCESS_DIR_EXPIRES_BY_TYPE:
        if (merged->expires_by_type == NULL) {
            merged->expires_by_type = ngx_array_create(pool, 4,
                                                       sizeof(htaccess_header_t));
        }
        if (merged->expires_by_type != NULL) {
            htaccess_header_t *h = ngx_array_push(merged->expires_by_type);
            if (h != NULL) {
                h->name = d->u.header.name;
                h->value = d->u.header.value;
            }
        }
        break;

    case HTACCESS_DIR_EXPIRES_DEFAULT:
        merged->expires_default = d->u.str;
        break;

    case HTACCESS_DIR_ERROR_DOCUMENT:
        if (merged->error_documents == NULL) {
            merged->error_documents = ngx_array_create(pool, 2,
                sizeof(htaccess_error_document_t));
        }
        if (merged->error_documents != NULL) {
            htaccess_error_document_t *e = ngx_array_push(
                merged->error_documents);
            if (e != NULL) {
                *e = d->u.error_doc;
            }
        }
        break;

    case HTACCESS_DIR_OPTIONS:
        if (d->u.flag) {
            merged->options |= 1;
        }
        break;

    case HTACCESS_DIR_DIRECTORY_INDEX:
        merged->directory_index = d->u.str;
        break;

    default:
        break;
    }
}


static ngx_int_t
htaccess_merge_parsed(ngx_http_request_t *r, htaccess_merged_ctx_t *merged,
    htaccess_dir_ctx_t *dir, htaccess_parsed_file_t *parsed)
{
    htaccess_directive_t  *d;
    ngx_uint_t             i;

    if (parsed == NULL || parsed->directives == NULL) {
        return NGX_OK;
    }

    d = parsed->directives->elts;
    for (i = 0; i < parsed->directives->nelts; i++) {
        htaccess_merge_directive(r->pool, merged, dir, &d[i]);
    }

    return NGX_OK;
}


static u_char *
htaccess_join_path(ngx_pool_t *pool, ngx_str_t *root, u_char *suffix,
    size_t suffix_len, ngx_str_t *filename, size_t *dir_len)
{
    u_char  *p, *data;
    size_t   len;

    len = root->len + suffix_len + filename->len + 3;
    data = ngx_pnalloc(pool, len);
    if (data == NULL) {
        return NULL;
    }

    p = ngx_cpymem(data, root->data, root->len);
    if (root->len == 0 || root->data[root->len - 1] != '/') {
        *p++ = '/';
    }

    if (suffix_len) {
        p = ngx_cpymem(p, suffix, suffix_len);
        if (suffix_len == 0 || suffix[suffix_len - 1] != '/') {
            *p++ = '/';
        }
    }

    if (dir_len) {
        *dir_len = p - data;
    }

    p = ngx_cpymem(p, filename->data, filename->len);
    *p = '\0';
    return data;
}


ngx_int_t
htaccess_build_merged_ctx(ngx_http_request_t *r,
    ngx_http_htaccess_loc_conf_t *conf, htaccess_cache_t *cache,
    htaccess_merged_ctx_t *merged)
{
    u_char                    *suffix, *path_data;
    size_t                     suffix_len, dir_len;
    ngx_str_t                  path, url_prefix;
    ngx_http_core_loc_conf_t  *clcf;
    htaccess_dir_ctx_t        *dir;
    htaccess_parsed_file_t    *parsed;
    ngx_uint_t                 i;
    ngx_int_t                  rc;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    ngx_memzero(merged, sizeof(htaccess_merged_ctx_t));
    merged->dirs = ngx_array_create(r->pool, 4, sizeof(htaccess_dir_ctx_t));
    if (merged->dirs == NULL) {
        return NGX_ERROR;
    }

    path_data = htaccess_join_path(r->pool, &clcf->root, (u_char *) "",
                                   0, &conf->filename, NULL);
    if (path_data == NULL) {
        return NGX_ERROR;
    }

    path.data = path_data;
    path.len = ngx_strlen(path_data);

    rc = htaccess_load_file(r, cache, conf, &path, &parsed);
    if (rc == NGX_OK) {
        dir = ngx_array_push(merged->dirs);
        if (dir == NULL) {
            return NGX_ERROR;
        }
        ngx_memzero(dir, sizeof(htaccess_dir_ctx_t));
        dir->dir_path = clcf->root;
        dir->url_prefix.data = (u_char *) "";
        dir->url_prefix.len = 0;
        dir->rewrite_base.data = (u_char *) "/";
        dir->rewrite_base.len = 1;
        dir->rewrite_engine = 0;
        htaccess_merge_parsed(r, merged, dir, parsed);
    }

    suffix_len = 0;
    suffix = ngx_pnalloc(r->pool, r->uri.len);
    if (suffix == NULL) {
        return NGX_ERROR;
    }

    for (i = 1; i < r->uri.len; i++) {
        if (r->uri.data[i] != '/') {
            continue;
        }

        suffix_len = i - 1;
        if (suffix_len > 0) {
            ngx_memcpy(suffix, r->uri.data + 1, suffix_len);
        }

        path_data = htaccess_join_path(r->pool, &clcf->root, suffix,
                                       suffix_len, &conf->filename, &dir_len);
        if (path_data == NULL) {
            return NGX_ERROR;
        }

        path.data = path_data;
        path.len = ngx_strlen(path_data);

        rc = htaccess_load_file(r, cache, conf, &path, &parsed);
        if (rc != NGX_OK) {
            continue;
        }

        dir = ngx_array_push(merged->dirs);
        if (dir == NULL) {
            return NGX_ERROR;
        }

        ngx_memzero(dir, sizeof(htaccess_dir_ctx_t));
        dir->dir_path.data = path_data;
        dir->dir_path.len = dir_len;
        url_prefix.data = r->uri.data;
        url_prefix.len = i;
        dir->url_prefix = url_prefix;
        dir->rewrite_base.data = (u_char *) "/";
        dir->rewrite_base.len = 1;
        dir->rewrite_engine = 0;
        htaccess_merge_parsed(r, merged, dir, parsed);
    }

    return NGX_OK;
}

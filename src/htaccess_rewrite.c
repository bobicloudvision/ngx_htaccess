
#include "htaccess_directives.h"


static ngx_str_t *
htaccess_env_get(htaccess_request_state_t *state, ngx_str_t *name)
{
    htaccess_env_var_t  *vars;
    ngx_uint_t           i;

    if (state->env == NULL) {
        return NULL;
    }

    vars = state->env->elts;
    for (i = 0; i < state->env->nelts; i++) {
        if (vars[i].name.len == name->len
            && ngx_strncmp(vars[i].name.data, name->data, name->len) == 0)
        {
            return &vars[i].value;
        }
    }

    return NULL;
}


static void
htaccess_env_set(ngx_pool_t *pool, htaccess_request_state_t *state,
    ngx_str_t *name, ngx_str_t *value)
{
    htaccess_env_var_t  *var;

    if (state->env == NULL) {
        state->env = ngx_array_create(pool, 4, sizeof(htaccess_env_var_t));
        if (state->env == NULL) {
            return;
        }
    }

    var = ngx_array_push(state->env);
    if (var == NULL) {
        return;
    }

    var->name = *name;
    var->value = *value;
}


static ngx_int_t
htaccess_file_test(ngx_str_t *filename, htaccess_cond_op_t op)
{
    ngx_file_info_t  fi;

    switch (op) {

    case HTACCESS_COND_OP_FILE:
        return ngx_file_info(filename->data, &fi) != NGX_FILE_ERROR
            && !ngx_is_dir(&fi);

    case HTACCESS_COND_OP_NOTFILE:
        return ngx_file_info(filename->data, &fi) == NGX_FILE_ERROR
            || ngx_is_dir(&fi);

    case HTACCESS_COND_OP_DIR:
        return ngx_file_info(filename->data, &fi) != NGX_FILE_ERROR
            && ngx_is_dir(&fi);

    case HTACCESS_COND_OP_NOTDIR:
        return ngx_file_info(filename->data, &fi) == NGX_FILE_ERROR
            || !ngx_is_dir(&fi);

    case HTACCESS_COND_OP_SYMLINK:
        return ngx_file_info(filename->data, &fi) != NGX_FILE_ERROR
            && ngx_is_link(&fi);

    case HTACCESS_COND_OP_NOTSYMLINK:
        return ngx_file_info(filename->data, &fi) == NGX_FILE_ERROR
            || !ngx_is_link(&fi);

    case HTACCESS_COND_OP_EXISTS:
        return ngx_file_info(filename->data, &fi) != NGX_FILE_ERROR;

    case HTACCESS_COND_OP_NOTEXISTS:
        return ngx_file_info(filename->data, &fi) == NGX_FILE_ERROR;

    default:
        return NGX_DECLINED;
    }
}


static ngx_int_t
htaccess_test_cond(ngx_http_request_t *r, htaccess_request_state_t *state,
    htaccess_rewrite_cond_t *cond)
{
    ngx_str_t       value, header;
    ngx_table_elt_t*h;
    ngx_list_part_t*part;
    ngx_uint_t       i;

    switch (cond->test) {

    case HTACCESS_COND_TEST_FILENAME:
        return htaccess_file_test(&state->filename, cond->op);

    case HTACCESS_COND_TEST_URI:
    case HTACCESS_COND_TEST_QUERY_STRING:
        if (cond->test == HTACCESS_COND_TEST_URI) {
            value = state->uri;
        } else {
            value = state->query_string;
        }
        break;

    case HTACCESS_COND_TEST_HTTP:
        value.len = 0;
        part = &r->headers_in.headers.part;
        header = cond->name;
        while (part) {
            h = part->elts;
            for (i = 0; i < part->nelts; i++) {
                if (h[i].key.len == header.len
                    && ngx_strncasecmp(h[i].key.data, header.data, header.len)
                       == 0)
                {
                    value = h[i].value;
                    goto http_found;
                }
            }
            part = part->next;
        }
http_found:
        break;

    case HTACCESS_COND_TEST_ENV:
        {
            ngx_str_t *v = htaccess_env_get(state, &cond->name);
            if (v == NULL) {
                value.len = 0;
                value.data = NULL;
            } else {
                value = *v;
            }
        }
        break;

    default:
        return NGX_DECLINED;
    }

    if (cond->op == HTACCESS_COND_OP_REGEX || cond->op == HTACCESS_COND_OP_NREGEX)
    {
        ngx_regex_compile_t  cc;
        ngx_int_t            erc;

        ngx_memzero(&cc, sizeof(ngx_regex_compile_t));
        cc.pattern = cond->value;
        cc.pool = r->pool;
        cc.err.len = 0;
        cc.err.data = NULL;

        if (ngx_regex_compile(&cc) != NGX_OK) {
            return NGX_ERROR;
        }

        erc = ngx_regex_exec(cc.regex, &value, NULL, 0);
        if (cond->op == HTACCESS_COND_OP_NREGEX) {
            return erc == NGX_REGEX_NO_MATCHED ? NGX_OK : NGX_DECLINED;
        }
        return erc >= 0 ? NGX_OK : NGX_DECLINED;
    }

    if (cond->value.len == 0) {
        return NGX_OK;
    }

    if (value.len >= cond->value.len
        && ngx_strncmp(value.data, cond->value.data, cond->value.len) == 0)
    {
        return NGX_OK;
    }

    return NGX_DECLINED;
}


static ngx_int_t
htaccess_match_conditions(ngx_http_request_t *r,
    htaccess_request_state_t *state, htaccess_rewrite_rule_t *rule)
{
    htaccess_rewrite_cond_t  *conds;
    ngx_uint_t                i;
    ngx_int_t                 rc;

    if (rule->conditions == NULL || rule->conditions->nelts == 0) {
        return NGX_OK;
    }

    conds = rule->conditions->elts;

    for (i = 0; i < rule->conditions->nelts; i++) {
        rc = htaccess_test_cond(r, state, &conds[i]);
        if (conds[i].or_next) {
            if (rc == NGX_OK) {
                return NGX_OK;
            }
            continue;
        }

        if (rc != NGX_OK) {
            return NGX_DECLINED;
        }
    }

    return NGX_OK;
}


static ngx_int_t
htaccess_compile_rule(ngx_pool_t *pool, htaccess_rewrite_rule_t *rule)
{
    ngx_regex_compile_t  cc;

    if (rule->regex != NULL) {
        return NGX_OK;
    }

    ngx_memzero(&cc, sizeof(ngx_regex_compile_t));
    cc.pattern = rule->pattern;
    cc.pool = pool;
    cc.options = rule->nocase ? NGX_REGEX_CASELESS : 0;
    cc.err.len = 0;
    cc.err.data = NULL;

    if (ngx_regex_compile(&cc) != NGX_OK) {
        return NGX_ERROR;
    }

    rule->regex = cc.regex;

    rule->regex_nc = rule->nocase;
    return NGX_OK;
}


static ngx_str_t
htaccess_relative_uri(ngx_str_t *uri, ngx_str_t *prefix)
{
    ngx_str_t  rel;

    if (prefix->len == 0 || uri->len <= prefix->len) {
        if (uri->len > 1 && uri->data[0] == '/') {
            rel.data = uri->data + 1;
            rel.len = uri->len - 1;
            return rel;
        }
        return *uri;
    }

    rel.data = uri->data + prefix->len;
    rel.len = uri->len - prefix->len;
    if (rel.len > 0 && rel.data[0] == '/') {
        rel.data++;
        rel.len--;
    }

    return rel;
}


static ngx_int_t
htaccess_apply_substitution(ngx_pool_t *pool, htaccess_rewrite_rule_t *rule,
    ngx_str_t *input, ngx_str_t *base, ngx_str_t *out)
{
    u_char  *p, *dst;
    size_t   len;

    if (rule->substitution.len == 1 && rule->substitution.data[0] == '-') {
        *out = *input;
        return NGX_OK;
    }

    if (rule->substitution.len > 0 && rule->substitution.data[0] == '/') {
        out->len = rule->substitution.len;
        out->data = ngx_pnalloc(pool, out->len);
        if (out->data == NULL) {
            return NGX_ERROR;
        }
        ngx_memcpy(out->data, rule->substitution.data, out->len);
        return NGX_OK;
    }

    if (rule->substitution.len > 7
        && ngx_strncmp(rule->substitution.data, "http://", 7) == 0)
    {
        *out = rule->substitution;
        return NGX_OK;
    }

    if (rule->substitution.len > 8
        && ngx_strncmp(rule->substitution.data, "https://", 8) == 0)
    {
        *out = rule->substitution;
        return NGX_OK;
    }

    len = base->len + rule->substitution.len + 1;
    dst = ngx_pnalloc(pool, len);
    if (dst == NULL) {
        return NGX_ERROR;
    }

    p = dst;

    if (base->len) {
        p = ngx_cpymem(p, base->data, base->len);
    }

    p = ngx_cpymem(p, rule->substitution.data, rule->substitution.len);
    out->data = dst;
    out->len = p - dst;

    return NGX_OK;
}


static void
htaccess_apply_env_flags(ngx_pool_t *pool, htaccess_request_state_t *state,
    htaccess_rewrite_rule_t *rule, ngx_http_request_t *r)
{
    ngx_str_t    *env, name, value, *header;
    u_char       *colon;
    ngx_uint_t    i;

    if (rule->env_vars == NULL) {
        return;
    }

    env = rule->env_vars->elts;
    for (i = 0; i < rule->env_vars->nelts; i++) {
        colon = ngx_strlchr(env[i].data, env[i].data + env[i].len, ':');
        if (colon == NULL) {
            continue;
        }

        name.data = env[i].data;
        name.len = colon - env[i].data;
        value.data = colon + 1;
        value.len = env[i].data + env[i].len - value.data;

        if (value.len >= 6
            && ngx_strncmp(value.data, "%{HTTP:", 6) == 0)
        {
            ngx_str_t hname;
            hname.data = value.data + 6;
            hname.len = value.len - 7;
            if (value.data[value.len - 1] == '}') {
                hname.len--;
            }
            header = NULL;
            {
                ngx_list_part_t  *part = &r->headers_in.headers.part;
                ngx_table_elt_t  *h;
                ngx_uint_t        j;
                while (part) {
                    h = part->elts;
                    for (j = 0; j < part->nelts; j++) {
                        if (h[j].key.len == hname.len
                            && ngx_strncasecmp(h[j].key.data, hname.data,
                                               hname.len) == 0)
                        {
                            header = &h[j].value;
                            break;
                        }
                    }
                    if (header) break;
                    part = part->next;
                }
            }
            if (header) {
                htaccess_env_set(pool, state, &name, header);
            }
            continue;
        }

        htaccess_env_set(pool, state, &name, &value);
    }
}


htaccess_result_t
htaccess_apply_rewrite(ngx_http_request_t *r,
    ngx_http_htaccess_loc_conf_t *conf, htaccess_merged_ctx_t *merged,
    htaccess_request_state_t *state)
{
    htaccess_dir_ctx_t       *dirs;
    htaccess_directive_t     *directives;
    htaccess_rewrite_rule_t  *rule;
    ngx_uint_t                i, j;
    ngx_str_t                 rel, out, uri;
    ngx_int_t                 rc;

    if (merged->dirs == NULL) {
        return HTACCESS_RESULT_DECLINED;
    }

    dirs = merged->dirs->elts;

    for (i = 0; i < merged->dirs->nelts; i++) {
        if (!dirs[i].rewrite_engine || dirs[i].directives == NULL) {
            continue;
        }

        directives = dirs[i].directives->elts;
        for (j = 0; j < dirs[i].directives->nelts; j++) {
            if (directives[j].id != HTACCESS_DIR_REWRITE_RULE) {
                continue;
            }

            rule = &directives[j].u.rewrite_rule;

            if (htaccess_compile_rule(r->pool, rule) != NGX_OK) {
                continue;
            }

            if (htaccess_match_conditions(r, state, rule) != NGX_OK) {
                continue;
            }

            rel = htaccess_relative_uri(&state->uri, &dirs[i].url_prefix);

            rc = ngx_regex_exec(rule->regex, &rel, NULL, 0);
            if (rc == NGX_REGEX_NO_MATCHED) {
                continue;
            }
            if (rc < 0) {
                return HTACCESS_RESULT_ERROR;
            }

            htaccess_apply_env_flags(r->pool, state, rule, r);

            if (rule->substitution.len == 1
                && rule->substitution.data[0] == '-')
            {
                if (rule->last || rule->end) {
                    return HTACCESS_RESULT_DECLINED;
                }
                continue;
            }

            if (htaccess_apply_substitution(r->pool, rule, &rel,
                    &dirs[i].rewrite_base, &out) != NGX_OK)
            {
                return HTACCESS_RESULT_ERROR;
            }

            if (rule->redirect) {
                state->uri = out;
                state->redirect_code = rule->redirect_code
                    ? rule->redirect_code : NGX_HTTP_MOVED_TEMPORARILY;
                return HTACCESS_RESULT_REDIRECT;
            }

            if (out.len > 0 && out.data[0] != '/') {
                uri.len = out.len + 1;
                uri.data = ngx_pnalloc(r->pool, uri.len);
                if (uri.data == NULL) {
                    return HTACCESS_RESULT_ERROR;
                }
                uri.data[0] = '/';
                ngx_memcpy(uri.data + 1, out.data, out.len);
                out = uri;
            }

            state->uri = out;

            if (rule->last || rule->end) {
                if (ngx_strncmp(state->uri.data, r->uri.data, r->uri.len) == 0
                    && state->uri.len == r->uri.len)
                {
                    return HTACCESS_RESULT_DECLINED;
                }
                return HTACCESS_RESULT_OK;
            }
        }
    }

    return HTACCESS_RESULT_DECLINED;
}

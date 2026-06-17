
#ifndef HTACCESS_DIRECTIVES_H
#define HTACCESS_DIRECTIVES_H

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


#define HTACCESS_MAX_REDIRECTS_DEFAULT  10
#define HTACCESS_CACHE_MAX_DEFAULT        1024


typedef enum {
    HTACCESS_DIR_REWRITE_ENGINE = 0,
    HTACCESS_DIR_REWRITE_BASE,
    HTACCESS_DIR_REWRITE_COND,
    HTACCESS_DIR_REWRITE_RULE,
    HTACCESS_DIR_REDIRECT,
    HTACCESS_DIR_REDIRECT_MATCH,
    HTACCESS_DIR_REDIRECT_PERMANENT,
    HTACCESS_DIR_REDIRECT_TEMP,
    HTACCESS_DIR_AUTH_TYPE,
    HTACCESS_DIR_AUTH_NAME,
    HTACCESS_DIR_AUTH_USER_FILE,
    HTACCESS_DIR_REQUIRE,
    HTACCESS_DIR_ORDER,
    HTACCESS_DIR_ALLOW,
    HTACCESS_DIR_DENY,
    HTACCESS_DIR_HEADER,
    HTACCESS_DIR_EXPIRES_ACTIVE,
    HTACCESS_DIR_EXPIRES_BY_TYPE,
    HTACCESS_DIR_EXPIRES_DEFAULT,
    HTACCESS_DIR_ERROR_DOCUMENT,
    HTACCESS_DIR_OPTIONS,
    HTACCESS_DIR_DIRECTORY_INDEX
} htaccess_directive_id_t;


typedef enum {
    HTACCESS_COND_TEST_FILENAME = 0,
    HTACCESS_COND_TEST_URI,
    HTACCESS_COND_TEST_QUERY_STRING,
    HTACCESS_COND_TEST_HTTP,
    HTACCESS_COND_TEST_ENV,
    HTACCESS_COND_TEST_FSPATH,
    HTACCESS_COND_TEST_OTHER
} htaccess_cond_test_t;


typedef enum {
    HTACCESS_COND_OP_EQ = 0,
    HTACCESS_COND_OP_NE,
    HTACCESS_COND_OP_LT,
    HTACCESS_COND_OP_GT,
    HTACCESS_COND_OP_LE,
    HTACCESS_COND_OP_GE,
    HTACCESS_COND_OP_REGEX,
    HTACCESS_COND_OP_NREGEX,
    HTACCESS_COND_OP_FILE,
    HTACCESS_COND_OP_NOTFILE,
    HTACCESS_COND_OP_DIR,
    HTACCESS_COND_OP_NOTDIR,
    HTACCESS_COND_OP_SYMLINK,
    HTACCESS_COND_OP_NOTSYMLINK,
    HTACCESS_COND_OP_EXISTS,
    HTACCESS_COND_OP_NOTEXISTS
} htaccess_cond_op_t;


typedef struct {
    htaccess_cond_test_t   test;
    ngx_str_t              name;
    htaccess_cond_op_t     op;
    ngx_str_t              value;
    ngx_uint_t             or_next;
} htaccess_rewrite_cond_t;


typedef struct {
    ngx_str_t                   pattern;
    ngx_str_t                   substitution;
    ngx_uint_t                  last;
    ngx_uint_t                  end;
    ngx_uint_t                  nocase;
    ngx_uint_t                  redirect;
    ngx_uint_t                  redirect_code;
    ngx_uint_t                  chain;
    ngx_uint_t                  chain_in;
    ngx_uint_t                  qsa;
    ngx_array_t                *env_vars;   /* ngx_str_t pairs "VAR:val" */
    ngx_array_t                *conditions; /* htaccess_rewrite_cond_t */
    ngx_regex_t                *regex;
    ngx_uint_t                  regex_nc;
} htaccess_rewrite_rule_t;


typedef struct {
    ngx_str_t     from;
    ngx_str_t     to;
    ngx_uint_t    permanent;
    ngx_regex_t  *regex;
} htaccess_alias_redirect_t;


typedef struct {
    ngx_str_t     action;
    ngx_str_t     name;
    ngx_str_t     value;
    ngx_str_t     target;
    ngx_uint_t    always;
} htaccess_header_t;


typedef struct {
    ngx_str_t     pattern;
    ngx_uint_t    deny;
    ngx_regex_t  *regex;
} htaccess_files_rule_t;


typedef struct {
    ngx_uint_t    code;
    ngx_str_t     uri;
} htaccess_error_document_t;


typedef struct {
    htaccess_directive_id_t  id;
    ngx_str_t                files_match;
    union {
        ngx_flag_t                  flag;
        ngx_str_t                   str;
        htaccess_rewrite_rule_t     rewrite_rule;
        htaccess_rewrite_cond_t     rewrite_cond;
        htaccess_alias_redirect_t   alias;
        htaccess_header_t           header;
        htaccess_error_document_t   error_doc;
    } u;
} htaccess_directive_t;


typedef struct {
    ngx_str_t              dir_path;
    ngx_str_t              url_prefix;
    ngx_str_t              rewrite_base;
    ngx_flag_t             rewrite_engine;
    ngx_array_t           *directives;
} htaccess_dir_ctx_t;


typedef struct {
    ngx_array_t           *dirs;          /* htaccess_dir_ctx_t */
    ngx_str_t              auth_type;
    ngx_str_t              auth_name;
    ngx_str_t              auth_user_file;
    ngx_array_t           *requires;
    ngx_str_t              order;
    ngx_array_t           *allows;
    ngx_array_t           *denies;
    ngx_array_t           *headers;
    ngx_flag_t             expires_active;
    ngx_array_t           *expires_by_type;
    ngx_str_t              expires_default;
    ngx_array_t           *error_documents;
    ngx_uint_t             options;
    ngx_str_t              directory_index;
    ngx_array_t           *files_rules;     /* htaccess_files_rule_t */
} htaccess_merged_ctx_t;


typedef struct {
    ngx_array_t           *directives;
    ngx_pool_t            *pool;
} htaccess_parsed_file_t;


typedef struct {
    ngx_queue_t            queue;
    ngx_str_t              path;
    time_t                 mtime;
    off_t                  size;
    ngx_uint_t             absent;
    htaccess_parsed_file_t *parsed;
} htaccess_cache_entry_t;


typedef struct {
    ngx_queue_t            lru;
    ngx_uint_t             count;
    ngx_uint_t             max_entries;
} htaccess_cache_t;


typedef struct {
    ngx_flag_t             enable;
    ngx_str_t              filename;
    ngx_flag_t             cache_enable;
    ngx_uint_t             cache_max_entries;
    ngx_uint_t             max_redirects;
} ngx_http_htaccess_loc_conf_t;


typedef struct {
    ngx_uint_t             cache_max_entries;
} ngx_http_htaccess_main_conf_t;


typedef struct {
    ngx_str_t               name;
    ngx_str_t               value;
} htaccess_env_var_t;


typedef enum {
    HTACCESS_RESULT_DECLINED = 0,
    HTACCESS_RESULT_OK,
    HTACCESS_RESULT_REDIRECT,
    HTACCESS_RESULT_FORBIDDEN,
    HTACCESS_RESULT_UNAUTHORIZED,
    HTACCESS_RESULT_ERROR
} htaccess_result_t;


typedef struct {
    ngx_str_t               uri;
    ngx_str_t               filename;
    ngx_str_t               query_string;
    ngx_array_t            *env;          /* htaccess_env_var_t */
    ngx_uint_t              redirect_code;
} htaccess_request_state_t;


/* lexer / parser */
ngx_int_t htaccess_lex_line(u_char **pos, u_char *end, ngx_array_t *tokens,
    ngx_pool_t *pool);
htaccess_parsed_file_t *htaccess_parse(ngx_pool_t *pool, u_char *start,
    u_char *end);
ngx_int_t htaccess_parse_file(ngx_pool_t *pool, ngx_str_t *path,
    htaccess_parsed_file_t **parsed);

/* cache */
void htaccess_cache_init(htaccess_cache_t *cache, ngx_uint_t max_entries);
htaccess_cache_entry_t *htaccess_cache_lookup(htaccess_cache_t *cache,
    ngx_pool_t *pool, ngx_str_t *path, ngx_uint_t cache_enable);
ngx_int_t htaccess_cache_store(htaccess_cache_t *cache, ngx_pool_t *pool,
    ngx_str_t *path, time_t mtime, off_t size, ngx_uint_t absent,
    htaccess_parsed_file_t *parsed);

/* walk */
ngx_int_t htaccess_build_merged_ctx(ngx_http_request_t *r,
    ngx_http_htaccess_loc_conf_t *conf, htaccess_cache_t *cache,
    htaccess_merged_ctx_t *merged);

/* engines */
htaccess_result_t htaccess_apply_alias(ngx_http_request_t *r,
    htaccess_merged_ctx_t *merged, htaccess_request_state_t *state);
htaccess_result_t htaccess_apply_rewrite(ngx_http_request_t *r,
    ngx_http_htaccess_loc_conf_t *conf, htaccess_merged_ctx_t *merged,
    htaccess_request_state_t *state);
htaccess_result_t htaccess_apply_auth(ngx_http_request_t *r,
    htaccess_merged_ctx_t *merged);
htaccess_result_t htaccess_apply_files_deny(ngx_http_request_t *r,
    htaccess_merged_ctx_t *merged, ngx_str_t *uri);
htaccess_result_t htaccess_apply_headers(ngx_http_request_t *r,
    htaccess_merged_ctx_t *merged);
htaccess_result_t htaccess_apply_options(ngx_http_request_t *r,
    htaccess_merged_ctx_t *merged);

ngx_int_t htaccess_execute(ngx_http_request_t *r,
    ngx_http_htaccess_loc_conf_t *conf, htaccess_cache_t *cache);

ngx_int_t htaccess_expand(ngx_pool_t *pool, ngx_http_request_t *r,
    htaccess_request_state_t *state, ngx_str_t *tpl, ngx_str_t *match,
    int *captures, ngx_uint_t ncaptures, ngx_str_t *out);

ngx_str_t *htaccess_env_get(htaccess_request_state_t *state, ngx_str_t *name);
void htaccess_env_set(ngx_pool_t *pool, htaccess_request_state_t *state,
    ngx_str_t *name, ngx_str_t *value);


#endif /* HTACCESS_DIRECTIVES_H */

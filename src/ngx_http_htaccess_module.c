
#include "htaccess_directives.h"


static ngx_int_t ngx_http_htaccess_handler(ngx_http_request_t *r);
static void *ngx_http_htaccess_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_htaccess_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_htaccess_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
static ngx_int_t ngx_http_htaccess_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_htaccess_init_process(ngx_cycle_t *cycle);


static ngx_command_t ngx_http_htaccess_commands[] = {

    { ngx_string("htaccess"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF
        | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_htaccess_loc_conf_t, enable),
      NULL },

    { ngx_string("htaccess_file"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF
        | NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_htaccess_loc_conf_t, filename),
      NULL },

    { ngx_string("htaccess_cache"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF
        | NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_htaccess_loc_conf_t, cache_enable),
      NULL },

    { ngx_string("htaccess_cache_max_entries"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_htaccess_main_conf_t, cache_max_entries),
      NULL },

    { ngx_string("htaccess_max_redirects"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF
        | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_htaccess_loc_conf_t, max_redirects),
      NULL },

      ngx_null_command
};


static ngx_http_module_t ngx_http_htaccess_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_htaccess_init,                /* postconfiguration */
    ngx_http_htaccess_create_main_conf,    /* create main configuration */
    NULL,                                  /* init main configuration */
    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */
    ngx_http_htaccess_create_loc_conf,     /* create location configuration */
    ngx_http_htaccess_merge_loc_conf       /* merge location configuration */
};


ngx_module_t ngx_http_htaccess_module = {
    NGX_MODULE_V1,
    &ngx_http_htaccess_module_ctx,         /* module context */
    ngx_http_htaccess_commands,            /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_http_htaccess_init_process,        /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_htaccess_main_conf_t  *htaccess_main_conf;
static htaccess_cache_t                htaccess_worker_cache;


static void *
ngx_http_htaccess_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_htaccess_main_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_htaccess_main_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->cache_max_entries = HTACCESS_CACHE_MAX_DEFAULT;

    return conf;
}


static void *
ngx_http_htaccess_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_htaccess_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_htaccess_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->enable = NGX_CONF_UNSET;
    conf->cache_enable = NGX_CONF_UNSET;
    conf->max_redirects = NGX_CONF_UNSET_UINT;

    return conf;
}


static char *
ngx_http_htaccess_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_htaccess_loc_conf_t  *prev = parent;
    ngx_http_htaccess_loc_conf_t  *conf = child;

    ngx_conf_merge_value(conf->enable, prev->enable, 0);
    ngx_conf_merge_str_value(conf->filename, prev->filename, ".htaccess");
    ngx_conf_merge_value(conf->cache_enable, prev->cache_enable, 1);
    ngx_conf_merge_uint_value(conf->max_redirects, prev->max_redirects,
                              HTACCESS_MAX_REDIRECTS_DEFAULT);

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_htaccess_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    htaccess_main_conf = ngx_http_conf_get_module_main_conf(cf,
                                                            ngx_http_htaccess_module);

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_htaccess_handler;

    return NGX_OK;
}


static ngx_int_t
ngx_http_htaccess_init_process(ngx_cycle_t *cycle)
{
    ngx_uint_t  max;

    max = htaccess_main_conf
        ? htaccess_main_conf->cache_max_entries : HTACCESS_CACHE_MAX_DEFAULT;

    htaccess_cache_init(&htaccess_worker_cache, max);

    return NGX_OK;
}


static htaccess_cache_t *
ngx_http_htaccess_get_cache(void)
{
    return &htaccess_worker_cache;
}


static ngx_int_t
ngx_http_htaccess_handler(ngx_http_request_t *r)
{
    ngx_http_htaccess_loc_conf_t  *conf;

    conf = ngx_http_get_module_loc_conf(r, ngx_http_htaccess_module);

    if (!conf->enable) {
        return NGX_DECLINED;
    }

    return htaccess_execute(r, conf, ngx_http_htaccess_get_cache());
}

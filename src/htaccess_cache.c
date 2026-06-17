
#include "htaccess_directives.h"


void
htaccess_cache_init(htaccess_cache_t *cache, ngx_uint_t max_entries)
{
    ngx_queue_init(&cache->lru);
    cache->count = 0;
    cache->max_entries = max_entries ? max_entries : HTACCESS_CACHE_MAX_DEFAULT;
}


static void
htaccess_cache_evict(htaccess_cache_t *cache)
{
    ngx_queue_t             *q;
    htaccess_cache_entry_t  *e;

    if (ngx_queue_empty(&cache->lru)) {
        return;
    }

    q = ngx_queue_last(&cache->lru);
    e = ngx_queue_data(q, htaccess_cache_entry_t, queue);
    ngx_queue_remove(q);
    cache->count--;

    if (e->parsed != NULL && e->parsed->pool != NULL) {
        ngx_destroy_pool(e->parsed->pool);
    }

    ngx_free(e);
}


static htaccess_cache_entry_t *
htaccess_cache_find(htaccess_cache_t *cache, ngx_str_t *path)
{
    ngx_queue_t             *q;
    htaccess_cache_entry_t  *e;

    for (q = ngx_queue_head(&cache->lru);
         q != ngx_queue_sentinel(&cache->lru);
         q = ngx_queue_next(q))
    {
        e = ngx_queue_data(q, htaccess_cache_entry_t, queue);

        if (e->path.len == path->len
            && ngx_memcmp(e->path.data, path->data, path->len) == 0)
        {
            ngx_queue_remove(q);
            ngx_queue_insert_head(&cache->lru, &e->queue);
            return e;
        }
    }

    return NULL;
}


htaccess_cache_entry_t *
htaccess_cache_lookup(htaccess_cache_t *cache, ngx_pool_t *pool,
    ngx_str_t *path, ngx_uint_t cache_enable)
{
    htaccess_cache_entry_t  *e;
    ngx_file_info_t          fi;

    if (!cache_enable) {
        return NULL;
    }

    e = htaccess_cache_find(cache, path);
    if (e == NULL) {
        return NULL;
    }

    if (e->absent) {
        if (ngx_file_info(path->data, &fi) == NGX_FILE_ERROR) {
            return e;
        }
        htaccess_cache_evict(cache);
        return NULL;
    }

    if (ngx_file_info(path->data, &fi) == NGX_FILE_ERROR) {
        htaccess_cache_evict(cache);
        return NULL;
    }

    if (e->mtime == fi.st_mtime && e->size == fi.st_size) {
        return e;
    }

    htaccess_cache_evict(cache);
    return NULL;
}


ngx_int_t
htaccess_cache_store(htaccess_cache_t *cache, ngx_pool_t *pool,
    ngx_str_t *path, time_t mtime, off_t size, ngx_uint_t absent,
    htaccess_parsed_file_t *parsed)
{
    htaccess_cache_entry_t  *e;

    while (cache->count >= cache->max_entries) {
        htaccess_cache_evict(cache);
    }

    e = ngx_alloc(sizeof(htaccess_cache_entry_t) + path->len, ngx_cycle->log);
    if (e == NULL) {
        return NGX_ERROR;
    }

    ngx_memzero(e, sizeof(htaccess_cache_entry_t));
    e->path.len = path->len;
    e->path.data = (u_char *) e + sizeof(htaccess_cache_entry_t);
    ngx_memcpy(e->path.data, path->data, path->len);
    e->mtime = mtime;
    e->size = size;
    e->absent = absent;
    e->parsed = parsed;

    ngx_queue_insert_head(&cache->lru, &e->queue);
    cache->count++;

    return NGX_OK;
}

/*
    +--------------------------------------------------------------------+
    | PECL :: http                                                       |
    +--------------------------------------------------------------------+
    | Redistribution and use in source and binary forms, with or without |
    | modification, are permitted provided that the conditions mentioned |
    | in the accompanying LICENSE file are met.                          |
    +--------------------------------------------------------------------+
    | Copyright (c) 2004-2011, Michael Wallner <mike@php.net>            |
    +--------------------------------------------------------------------+
*/

#include "php_http_api.h"
#include "php_http_client.h"

#include <serf.h>

#if PHP_HTTP_HAVE_SERF

typedef struct php_http_client_serf {
	serf_context_t *context;
	apr_pool_t *pool;
} php_http_client_serf_t;


static void php_http_serf_progress_callback(void *progress_baton, apr_off_t read, apr_off_t write)
{
	serf_progress_t not_implemented;
	/* useless bullshit */
}

static apr_status_t php_http_serf_connection_setup(apr_socket_t *skt, serf_bucket_t **read_bkt, serf_bucket_t **write_bkt, void *setup_baton, apr_pool_t *pool)
{
	serf_connection_setup_t not_implmented;
	return APR_SUCCESS;
}

static void php_http_serf_connection_closed(serf_connection_t *conn, void *closed_baton, apr_status_t why, apr_pool_t *pool)
{
	serf_connection_closed_t not_implemented;
}

static serf_bucket_t *php_http_client_serf_response_acceptor(serf_request_t *request, serf_bucket_t *stream, void *acceptor_baton, apr_pool_t *pool)
{
	serf_response_acceptor_t not_implemented;
	return NULL;
}

static apr_status_t php_http_serf_response_handler(serf_request_t *request, serf_bucket_t *response, void *handler_baton, apr_pool_t *pool)
{
	serf_response_handler_t not_implemented;
	return APR_SUCCESS;
}

static apr_status_t php_http_serf_credentials_callback(char **username, char **password, serf_request_t *request, void *baton, int code, const char *authn_type, const char *realm, apr_pool_t *pool)
{
	serf_credentials_callback_t not_implemented;
	return APR_SUCCESS;
}

static apr_status_t php_http_serf_request_setup(serf_request_t *request, void *setup_baton, serf_bucket_t **req_bkt, serf_response_acceptor_t *acceptor, void **acceptor_baton, serf_response_handler_t *handler, void **handler_baton, apr_pool_t *pool)
{
	serf_request_setup_t not_implemented;
	return APR_SUCCESS;
}

static inline apr_pool_t *new_pool(apr_pool_t *parent TSRMLS_DC)
{
	apr_pool_t *pool = NULL;

	if (!parent) {
		parent = PHP_HTTP_G->serf.pool;
	}
	if (APR_SUCCESS != apr_pool_create(&pool, parent) || !pool) {
		zend_error(E_ERROR, "Failed to allocate apr pool");
	}

	return pool;
}

static void *php_http_serf_context_ctor(void *opaque, void *init_arg TSRMLS_DC)
{
	apr_pool_t *pool = new_pool(init_arg TSRMLS_CC);
	serf_context_t *serf = serf_context_create_ex(pool);

	if (serf) {
		apr_pool_userdata_setn(serf, "serf", NULL, pool);
	}
	return pool;
}

static void php_http_serf_context_dtor(void *opaque, void *handle TSRMLS_DC)
{
	apr_pool_destroy(handle);
}

static php_resource_factory_ops_t php_http_serf_context_resource_factory_ops = {
	php_http_serf_context_ctor,
	NULL,
	php_http_serf_context_dtor
}

struct serf_connection_init_data {
	php_http_client_t *client;
	apr_uri_t *uri;
};

typedef struct php_http_client_serf_handler {
	serf_connection_t *conn;
	php_http_client_t *client;
} php_http_client_serf_handler_t;

static void *php_http_serf_connection_ctor(void *opaque, void *init_arg TSRMLS_DC)
{
	struct serf_connection_init_data *data = init_arg;
	php_http_client_serf_t *serf = data->client->ctx;
	serf_connection_t *conn = NULL;

	serf_connection_create2(&conn, serf->context, data->uri, php_http_serf_connection_setup, data->client, php_http_client_serf_connection_closed, data->client);
}

static php_http_client_t *php_http_client_serf_init(php_http_client_t *h, void *pool)
{
	php_http_client_serf_t *serf;
	TSRMLS_FETCH_FROM_CTX(h->ts);

	if (!pool && !(pool = php_resource_factory_handle_ctor(h->rf, NULL) TSRMLS_CC)) {
		php_http_error(HE_WARNING, PHP_HTTP_E_CLIENT_POOL, "Failed to initialize serf context");
		return NULL;
	}

	serf = ecalloc(1, sizeof(*serf));
	serf->pool = pool;
	apr_pool_userdata_get(&serf->context, "serf", serf->pool);
	serf_context_set_progress_cb(serf->context, php_http_serf_progress_callback, h);
	h->ctx = serf;

	return h;
}

static void php_http_client_serf_dtor(php_http_client_t *h)
{
	php_http_client_serf_t *serf = h->ctx;
	TSRMLS_FETCH_FROM_CTX(h->ts);

	php_resource_factory_handle_dtor(h->rf, serf->pool TSRMLS_CC);

	efree(serf);
	h->ctx = NULL;
}
static void php_http_client_serf_reset(php_http_client_t *h);
static STATUS php_http_client_serf_exec(php_http_client_t *h);
static int php_http_client_serf_once(php_http_client_t *h);
static STATUS php_http_client_serf_wait(php_http_client_t *h, struct timeval *custom_timeout);


static php_resource_factory_t *create_rf(const char *url TSRMLS_DC)
{
	php_url *purl;
	php_resource_factory_t *rf = NULL;

	if ((purl = php_url_parse(url))) {
		char *id_str = NULL;
		size_t id_len = spprintf(&id_str, 0, "%s:%d", STR_PTR(purl->host), purl->port ? purl->port : 80);
		php_persistent_handle_factory_t *pf = php_persistent_handle_concede(NULL, ZEND_STRL("http\\Client\\Serf\\Request"), id_str, id_len, NULL, NULL TSRMLS_CC);

		if (pf) {
			rf = php_resource_factory_init(NULL, php_persistent_handle_get_resource_factory_ops(), pf, (void (*)(void*)) php_persistent_handle_abandon);
		}

		php_url_free(purl);
		efree(id_str);
	}

	if (!rf) {
		rf = php_resource_factory_init(NULL, &php_http_serf_connection_resource_factory_ops, NULL, NULL);
	}

	return rf;
}

static STATUS php_http_client_serf_enqueue(php_http_client_t *h, php_http_client_enqueue_t *enqueue)
{
	apr_pool_t *pool;
	php_http_client_serf_t *serf = h->ctx;
	TSRMLS_FETCH_FROM_CTX(h->ts);

	pool = new_pool(serf->pool);
	zend_llist_add_element(&h->requests, enqueue);
}
static STATUS php_http_client_serf_dequeue(php_http_client_t *h, php_http_client_enqueue_t *enqueue);
static STATUS php_http_client_serf_setopt(php_http_client_t *h, php_http_client_setopt_opt_t opt, void *arg);
static STATUS php_http_client_serf_getopt(php_http_client_t *h, php_http_client_getopt_opt_t opt, void *arg, void **res);

static php_http_client_ops_t php_http_client_serf_ops = {
	&php_http_serf_context_resource_factory_ops,
	php_http_client_serf_init,
	NULL /*php_http_client_serf_copy*/,
	php_http_client_serf_dtor,
	php_http_client_serf_reset,
	php_http_client_serf_exec,
	php_http_client_serf_wait,
	php_http_client_serf_once,
	php_http_client_serf_enqueue,
	php_http_client_serf_dequeue,
	php_http_client_serf_setopt,
	php_http_client_serf_getopt
};

PHP_MINIT_FUNCTION(http_client_serf)
{
	php_http_client_driver_t driver = {
		ZEND_STRL("serf"),
		&php_http_client_serf_ops
	};

	if (SUCCESS != php_http_client_driver_add(&driver)) {
		return FAILURE;
	}
	if (APR_SUCCESS != apr_pool_create(&PHP_HTTP_G->serf.pool, NULL)) {
		return FAILURE;
	}
	if (SUCCESS != php_persistent_handle_provide(ZEND_STRL("http\\Client\\Curl"), &php_http_serf_context_resource_factory_ops, NULL, NULL TSRMLS_CC)) {
		return FAILURE;
	}
	if (SUCCESS != php_persistent_handle_provide(ZEND_STRL("http\\Client\\Curl\\Request"), &php_http_serf_connection_resource_factory_ops, NULL, NULL TSRMLS_CC)) {
		return FAILURE;
	}


	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(http_client_serf)
{
	apr_pool_destroy(PHP_HTTP_G->serf.pool);
	PHP_HTTP_G->serf.pool = NULL;
	return SUCCESS;
}
#endif

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */

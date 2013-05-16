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
	php_http_client_t *client;
} php_http_client_serf_t;

typedef struct php_http_client_serf_handler {
	apr_pool_t *pool;
	apr_uri_t *uri;
	serf_connection_t *conn;
	serf_request_t *req;
	php_http_client_serf_t *serf;
	php_resource_factory_t *rf;
	php_http_client_enqueue_t queue;

	struct {
		php_http_message_parser_t *parser;
		php_http_message_t *message;
		php_http_buffer_t *buffer;
	} request;

	struct {
		php_http_message_parser_t *parser;
		php_http_message_t *message;
		php_http_buffer_t *buffer;
	} response;

#ifdef ZTS
	void ***ts;
#endif
} php_http_client_serf_handler_t;


static void php_http_serf_progress_callback(void *progress_baton, apr_off_t read, apr_off_t write)
{
	serf_progress_t not_implemented;
	/* useless bullshit */
}

#define not_implemented x;fprintf(stderr, "not implemented: %d\n",__LINE__);

static apr_status_t php_http_serf_connection_setup(apr_socket_t *skt, serf_bucket_t **read_bkt, serf_bucket_t **write_bkt, void *setup_baton, apr_pool_t *pool)
{
	php_http_client_serf_handler_t *serf = setup_baton;
	serf_bucket_alloc_t *a = serf_bucket_allocator_create(serf->pool, NULL, NULL);

	*read_bkt = serf_bucket_socket_create(skt, a);
	/* TODO: handle SSL */

	return APR_SUCCESS;
}

static void php_http_serf_connection_closed(serf_connection_t *conn, void *closed_baton, apr_status_t why, apr_pool_t *pool)
{
	serf_connection_closed_t not_implemented;
}

static serf_bucket_t *php_http_serf_response_acceptor(serf_request_t *request, serf_bucket_t *stream, void *acceptor_baton, apr_pool_t *pool)
{
	serf_bucket_alloc_t *a = serf_request_get_alloc(request);
	serf_bucket_t *b = serf_bucket_barrier_create(stream, a);

	return serf_bucket_response_create(b, a);
}
void errstr(apr_status_t s)
{
	char b[512];
	fprintf(stderr, "serf: %d - %s, %s\n", s, serf_error_string(s), apr_strerror(s, b, sizeof(b)));
}

static apr_status_t read_bkt(php_http_client_serf_handler_t *handler, serf_bucket_t *bkt)
{
	apr_status_t rv;

	do {
		const char *data_str = NULL;
		size_t data_len = 0;

		rv = serf_bucket_read(bkt, SERF_READ_ALL_AVAIL, &data_str, &data_len);
		php_http_buffer_append(handler->response.buffer, data_str, data_len);
		fprintf(stderr, "READ: %zu, %.*s\n", data_len, data_len, data_str);
		php_http_message_parser_parse(handler->response.parser, handler->response.buffer, 0, &handler->response.message);
	} while (APR_SUCCESS == rv);

	return rv;
}

static apr_status_t php_http_serf_response_handler(serf_request_t *request, serf_bucket_t *response, void *handler_baton, apr_pool_t *pool)
{
	php_http_client_serf_handler_t *handler = handler_baton;
	apr_status_t rv;

	if (!response) {
		return APR_EOF;
	}

	rv = read_bkt(handler, serf_bucket_response_get_headers(response));
	errstr(rv);

	if (!APR_STATUS_IS_EOF(rv)) {
		return rv;
	}

	rv = read_bkt(handler, response);
	errstr(rv);

	if (APR_STATUS_IS_EOF(rv)) {
		php_http_client_t *context = handler->serf->client;

		context->callback.response.func(context->callback.response.arg, context, &handler->queue, &handler->request.message, &handler->response.message);
	}

	return rv;
}

static apr_status_t php_http_serf_credentials_callback(char **username, char **password, serf_request_t *request, void *baton, int code, const char *authn_type, const char *realm, apr_pool_t *pool)
{
	serf_credentials_callback_t not_implemented;
	return APR_SUCCESS;
}

static apr_status_t php_http_serf_request_setup(serf_request_t *request, void *setup_baton, serf_bucket_t **req_bkt, serf_response_acceptor_t *acceptor, void **acceptor_baton, serf_response_handler_t *handler, void **handler_baton, apr_pool_t *pool)
{
	serf_bucket_t *body = NULL;
	serf_bucket_alloc_t *alloc = serf_request_get_alloc(request);
	php_http_client_serf_handler_t *serf_handler = setup_baton;

	*req_bkt = serf_request_bucket_request_create(request,
			serf_handler->queue.request->http.info.request.method,
			serf_handler->queue.request->http.info.request.url, body, alloc);
	*acceptor = php_http_serf_response_acceptor;
	*acceptor_baton = setup_baton;
	*handler = php_http_serf_response_handler;
	*handler_baton = setup_baton;

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
	serf_context_t *serf = serf_context_create(pool);

	if (!serf) {
		apr_pool_destroy(pool);
		return NULL;
	}

	apr_pool_userdata_setn(serf, "serf", NULL, pool);
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
};

struct serf_connection_init_data {
	apr_uri_t uri;
	apr_pool_t *pool;
	serf_connection_t *conn;
	php_http_client_t *client;
	php_http_client_serf_handler_t *handler;
};

static void *php_http_serf_connection_ctor(void *opaque, void *init_arg TSRMLS_DC)
{
	struct serf_connection_init_data *data = *(struct serf_connection_init_data **) init_arg;
	php_http_client_serf_t *serf = data->client->ctx;

	/*
	 * this all looks quite awkward, because we want the uri be allocated from the connection pool
	 */
	if (APR_SUCCESS != serf_connection_create2(&data->conn, serf->context, data->uri, php_http_serf_connection_setup, data->handler, php_http_serf_connection_closed, data->client, data->pool)) {
		return NULL;
	}
	apr_pool_userdata_setn(data->conn, "conn", (apr_status_t(*)(void*)) serf_connection_close, data->pool);
	return data->pool;
}

static void php_http_serf_connection_dtor(void *opaque, void *handle TSRMLS_DC)
{
	//apr_pool_destroy(handle);
}

static php_resource_factory_ops_t php_http_serf_connection_resource_factory_ops = {
	php_http_serf_connection_ctor,
	NULL,
	php_http_serf_connection_dtor
};

static php_http_client_serf_handler_t *php_http_client_serf_handler_init(php_http_client_t *h, struct serf_connection_init_data *data)
{
	php_http_client_serf_t *serf = h->ctx;
	php_http_client_serf_handler_t *handler = NULL;
	php_resource_factory_t *rf = NULL;
	php_persistent_handle_factory_t *phf;
	TSRMLS_FETCH_FROM_CTX(h->ts);

	phf = php_persistent_handle_concede(NULL, ZEND_STRL("http\\Client\\Serf\\Request"), data->uri.hostinfo, strlen(data->uri.hostinfo), NULL, NULL TSRMLS_CC);
	if (phf) {
		rf = php_resource_factory_init(NULL, php_persistent_handle_get_resource_factory_ops(), phf, (void(*)(void*)) php_persistent_handle_abandon);
	}

	if (!rf) {
		rf = php_resource_factory_init(NULL, &php_http_serf_connection_resource_factory_ops, NULL, NULL);
	}

	handler = ecalloc(1, sizeof(*handler));
	handler->rf = rf;
	handler->serf = serf;

	handler->response.buffer = php_http_buffer_init(NULL);
	handler->response.parser = php_http_message_parser_init(NULL TSRMLS_CC);
	handler->response.message = php_http_message_init(NULL, 0, NULL TSRMLS_CC);

	TSRMLS_SET_CTX(handler->ts);

	data->handler = handler;
	if (!(handler->pool = php_resource_factory_handle_ctor(rf, &data TSRMLS_CC))) {
		/* TODO: cleanup */
		return NULL;
	}

	apr_pool_userdata_get((void *) &handler->conn, "conn", handler->pool);

	if (!handler->conn) {
		/* TODO: cleanup */
		return NULL;
	}

	return handler;

}

static void php_http_client_serf_handler_dtor(php_http_client_serf_handler_t *handler)
{
	TSRMLS_FETCH_FROM_CTX(handler->ts);

	php_resource_factory_handle_dtor(handler->rf, handler->pool TSRMLS_CC);
	php_resource_factory_free(&handler->rf);

	php_http_buffer_free(&handler->response.buffer);
	php_http_message_free(&handler->response.message);
	php_http_message_parser_free(&handler->response.parser);

	efree(handler);
}

static php_http_client_t *php_http_client_serf_init(php_http_client_t *h, void *pool)
{
	php_http_client_serf_t *serf;
	TSRMLS_FETCH_FROM_CTX(h->ts);

	if (!pool && !(pool = php_resource_factory_handle_ctor(h->rf, NULL TSRMLS_CC))) {
		php_http_error(HE_WARNING, PHP_HTTP_E_CLIENT_POOL, "Failed to initialize serf context");
		return NULL;
	}

	serf = ecalloc(1, sizeof(*serf));
	serf->client = h;
	serf->pool = pool;
	apr_pool_userdata_get((void *) &serf->context, "serf", serf->pool);
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

static void php_http_client_serf_reset(php_http_client_t *h)
{

}

static STATUS php_http_client_serf_exec(php_http_client_t *h)
{
	php_http_client_serf_t *serf = h->ctx;
	apr_status_t rc;

	do {
		rc = serf_context_run(serf->context, SERF_DURATION_FOREVER, serf->pool);
	} while (rc == APR_SUCCESS);

	errstr(rc);

	return SUCCESS;
}

static int php_http_client_serf_once(php_http_client_t *h)
{
	return 0;
}

static STATUS php_http_client_serf_wait(php_http_client_t *h, struct timeval *custom_timeout)
{
	return FAILURE;
}


static void queue_dtor(php_http_client_enqueue_t *e)
{
	php_http_client_serf_handler_t *handler = e->opaque;

	if (handler->queue.dtor) {
		e->opaque = handler->queue.opaque;
		handler->queue.dtor(e);
	}
	php_http_client_serf_handler_dtor(handler);
}

static STATUS php_http_client_serf_handler_prepare(php_http_client_serf_handler_t *handler, struct serf_connection_init_data *data)
{
	handler->req = serf_connection_request_create(handler->conn, php_http_serf_request_setup, handler);

	return SUCCESS;
}

static STATUS php_http_client_serf_enqueue(php_http_client_t *h, php_http_client_enqueue_t *enqueue)
{
	apr_pool_t *pool;
	php_http_client_serf_t *serf = h->ctx;
	struct serf_connection_init_data data;
	php_http_client_serf_handler_t *handler;
	php_resource_factory_t *rf = NULL;
	TSRMLS_FETCH_FROM_CTX(h->ts);

	data.client = h;
	data.pool = new_pool(serf->pool TSRMLS_CC);
	if (APR_SUCCESS != apr_uri_parse(data.pool, enqueue->request->http.info.request.url, &data.uri)) {
		apr_pool_destroy(data.pool);
		php_http_error(HE_WARNING, PHP_HTTP_E_CLIENT, "Failed to parse URI '%s'", enqueue->request->http.info.request.url);
		return FAILURE;
	}

	handler = php_http_client_serf_handler_init(h, &data);
	if (!handler) {
		return FAILURE;
	}

	handler->queue = *enqueue;
	enqueue->opaque = handler;
	enqueue->dtor = queue_dtor;

	if (SUCCESS != php_http_client_serf_handler_prepare(handler, &data)) {
		php_http_client_serf_handler_dtor(handler);
		return FAILURE;
	}

	zend_llist_add_element(&h->requests, enqueue);

	return SUCCESS;
}

static int compare_queue(php_http_client_enqueue_t *e, void *ep)
{
	return e->request == ((php_http_client_enqueue_t *) ep)->request;
}

static STATUS php_http_client_serf_dequeue(php_http_client_t *h, php_http_client_enqueue_t *enqueue)
{
	zend_llist_del_element(&h->requests, enqueue, (int(*)(void*,void*)) compare_queue);
	return SUCCESS;
}

static STATUS php_http_client_serf_setopt(php_http_client_t *h, php_http_client_setopt_opt_t opt, void *arg)
{
	return FAILURE;
}

static STATUS php_http_client_serf_getopt(php_http_client_t *h, php_http_client_getopt_opt_t opt, void *arg, void **res)
{
	return FAILURE;
}

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
	if (APR_SUCCESS != apr_initialize()) {
		return FAILURE;
	}
	if (APR_SUCCESS != apr_pool_create(&PHP_HTTP_G->serf.pool, NULL)) {
		return FAILURE;
	}
	if (SUCCESS != php_persistent_handle_provide(ZEND_STRL("http\\Client\\Serf"), &php_http_serf_context_resource_factory_ops, NULL, NULL TSRMLS_CC)) {
		return FAILURE;
	}
	if (SUCCESS != php_persistent_handle_provide(ZEND_STRL("http\\Client\\Serf\\Request"), &php_http_serf_connection_resource_factory_ops, NULL, NULL TSRMLS_CC)) {
		return FAILURE;
	}


	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(http_client_serf)
{
	apr_pool_destroy(PHP_HTTP_G->serf.pool);
	PHP_HTTP_G->serf.pool = NULL;
	apr_terminate();
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

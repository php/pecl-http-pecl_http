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

#if PHP_HTTP_HAVE_SERF

#if defined(ZTS) && defined(PHP_HTTP_HAVE_OPENSSL)
#	define PHP_HTTP_NEED_OPENSSL_TSL
#	include <openssl/crypto.h>
#endif /* ZTS && PHP_HTTP_HAVE_OPENSSL */


#ifdef PHP_HTTP_NEED_OPENSSL_TSL
static MUTEX_T *php_http_openssl_tsl = NULL;

static void php_http_openssl_thread_lock(int mode, int n, const char * file, int line)
{
	if (mode & CRYPTO_LOCK) {
		tsrm_mutex_lock(php_http_openssl_tsl[n]);
	} else {
		tsrm_mutex_unlock(php_http_openssl_tsl[n]);
	}
}

static ulong php_http_openssl_thread_id(void)
{
	return (ulong) tsrm_thread_id();
}
#endif

PHP_MINIT_FUNCTION(http_serf)
{
	php_http_client_factory_driver_t driver = {
		php_http_serf_client_get_ops(),
		NULL,
		NULL
	};

#ifdef PHP_HTTP_NEED_OPENSSL_TSL
	/* mod_ssl, libpq or ext/curl might already have set thread lock callbacks */
	if (!CRYPTO_get_id_callback()) {
		int i, c = CRYPTO_num_locks();

		php_http_openssl_tsl = malloc(c * sizeof(MUTEX_T));

		for (i = 0; i < c; ++i) {
			php_http_openssl_tsl[i] = tsrm_mutex_alloc();
		}

		CRYPTO_set_id_callback(php_http_openssl_thread_id);
		CRYPTO_set_locking_callback(php_http_openssl_thread_lock);
	}
#endif

	if (SUCCESS != php_http_client_factory_add_driver(ZEND_STRL("serf"), &driver)) {
		return FAILURE;
	}

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(http_serf)
{
#ifdef PHP_HTTP_NEED_OPENSSL_TSL
	if (php_http_openssl_tsl) {
		int i, c = CRYPTO_num_locks();

		CRYPTO_set_id_callback(NULL);
		CRYPTO_set_locking_callback(NULL);

		for (i = 0; i < c; ++i) {
			tsrm_mutex_free(php_http_openssl_tsl[i]);
		}

		free(php_http_openssl_tsl);
		php_http_openssl_tsl = NULL;
	}
#endif
	return SUCCESS;
}

#endif /* PHP_HTTP_HAVE_SERF */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */


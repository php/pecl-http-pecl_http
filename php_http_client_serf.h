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

#ifndef PHP_HTTP_CLIENT_SERF_H
#define PHP_HTTP_CLIENT_SERF_H

#if PHP_HTTP_HAVE_SERF
struct php_http_serf_globals {
	apr_pool_t *pool;
};

PHP_MINIT_FUNCTION(http_client_serf);
PHP_MSHUTDOWN_FUNCTION(http_client_serf);
#endif /* PHP_HTTP_HAVE_SERF */

#endif /* PHP_HTTP_CLIENT_SERF_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */

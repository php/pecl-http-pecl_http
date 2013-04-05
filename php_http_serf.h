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

#ifndef PHP_HTTP_SERF_H
#define PHP_HTTP_SERF_H

#if PHP_HTTP_HAVE_SERF

#include <serf.h>

PHP_MINIT_FUNCTION(http_serf);
PHP_MSHUTDOWN_FUNCTION(http_serf);

#endif /* PHP_HTTP_HAVE_SERF */

#endif /* PHP_HTTP_SERF_H */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */


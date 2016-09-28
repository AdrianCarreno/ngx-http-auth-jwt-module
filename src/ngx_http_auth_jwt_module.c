/*
 * Tesla Government
 * @author joefitz
 */

#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <jwt.h>

#include <jansson.h>

typedef struct {
    ngx_str_t   auth_jwt_loginurl;
    ngx_str_t   auth_jwt_key;
	ngx_flag_t  auth_jwt_enabled;
} ngx_http_auth_jwt_loc_conf_t;

static ngx_int_t ngx_http_auth_jwt_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_auth_jwt_handler(ngx_http_request_t *r);
static void * ngx_http_auth_jwt_create_loc_conf(ngx_conf_t *cf);
static char * ngx_http_auth_jwt_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child);
static int hex_char_to_binary( char ch, char* ret );
static int hex_to_binary( const char* str, u_char* buf, int len );

static ngx_command_t  ngx_http_auth_jwt_commands[] = {

    { ngx_string("auth_jwt_loginurl"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_auth_jwt_loc_conf_t, auth_jwt_loginurl),
      NULL },

    { ngx_string("auth_jwt_key"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_auth_jwt_loc_conf_t, auth_jwt_key),
      NULL },
	  
	{ ngx_string("auth_jwt_enabled"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_auth_jwt_loc_conf_t, auth_jwt_enabled),
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_auth_jwt_module_ctx = {
    NULL,                        /* preconfiguration */
    ngx_http_auth_jwt_init,      /* postconfiguration */

    NULL,                        /* create main configuration */
    NULL,                        /* init main configuration */

    NULL,                        /* create server configuration */
    NULL,                        /* merge server configuration */

    ngx_http_auth_jwt_create_loc_conf,      /* create location configuration */
    ngx_http_auth_jwt_merge_loc_conf       /* merge location configuration */
};


ngx_module_t  ngx_http_auth_jwt_module = {
    NGX_MODULE_V1,
    &ngx_http_auth_jwt_module_ctx,     /* module context */
    ngx_http_auth_jwt_commands,        /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t ngx_http_auth_jwt_handler(ngx_http_request_t *r)
{
	ngx_int_t n;
	ngx_str_t jwtCookieName = ngx_string("rampartjwt");
	ngx_str_t jwtCookieVal;
	char* jwtCookieValChrPtr;
	ngx_http_auth_jwt_loc_conf_t  *jwtcf;
	u_char *keyBinary;
	jwt_t *jwt;
	int jwtParseReturnCode;
	jwt_alg_t alg;
	time_t exp;
	time_t now;
	ngx_str_t passportKeyCookieName = ngx_string("PassportKey");
	ngx_str_t passportKeyCookieVal;
	
	
    jwtcf = ngx_http_get_module_loc_conf(r, ngx_http_auth_jwt_module);
	
	if (!jwtcf->auth_jwt_enabled) 
	{
        return NGX_DECLINED;
    }
	
//	ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Key: %s, Enabled: %d", 
//			jwtcf->auth_jwt_key.data, 
//			jwtcf->auth_jwt_enabled);
	
	// get the cookie
	// TODO: the cookie name could be passed in dynamicallly
	n = ngx_http_parse_multi_header_lines(&r->headers_in.cookies, &jwtCookieName, &jwtCookieVal);
	if (n == NGX_DECLINED) 
	{
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "failed to obtain rampartjwt cookie");
		goto redirect;
    }
	
	// the cookie data is not necessarily null terminated... we need a null terminated character pointer
	jwtCookieValChrPtr = ngx_alloc(jwtCookieVal.len + 1, r->connection->log);
	ngx_memcpy(jwtCookieValChrPtr, jwtCookieVal.data, jwtCookieVal.len);
	*(jwtCookieValChrPtr+jwtCookieVal.len) = '\0';

//	ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "rampartjwt: %s %d", jwtCookieValChrPtr, jwtCookieVal.len);
	
	// convert key from hex to binary
	keyBinary = ngx_alloc(jwtcf->auth_jwt_key.len / 2, r->connection->log);
	if (0 != hex_to_binary((char *)jwtcf->auth_jwt_key.data, keyBinary, jwtcf->auth_jwt_key.len))
	{
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "failed to turn hex key into binary");
		goto redirect;
	}
	
	// validate the jwt
	jwtParseReturnCode = jwt_decode(&jwt, jwtCookieValChrPtr, keyBinary, jwtcf->auth_jwt_key.len / 2);
	if (jwtParseReturnCode != 0)
	{
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "failed to parse jwt");
		goto redirect;
	}
	
//	ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "parsed jwt:\n%s", jwt_dump_str(jwt, 1));
	
	// validate the algorithm
	alg = jwt_get_alg(jwt);
	if (alg != JWT_ALG_HS256)
	{
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "invalid algorithm in jwt %d", alg);
		goto redirect;
	}
	
	// validate the exp date of the JWT
	exp = (time_t)jwt_get_grant_int(jwt, "exp");
	now = time(NULL);
	if (exp < now)
	{
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "the jwt has expired");
		goto redirect;
	}
	
	// ensure that the user has a matching PassportKey cookie.
	// this can be removed once we and our partners no longer use the PassportKey cookie	
	n = ngx_http_parse_multi_header_lines(&r->headers_in.cookies, &passportKeyCookieName, &passportKeyCookieVal);
	if (n == NGX_DECLINED) {
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "failed to obtain passport cookie");
		goto redirect;
    };
	
	// compare both cookies
	if (ngx_strncmp(jwtCookieVal.data, passportKeyCookieVal.data, jwtCookieVal.len))
	{
		ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "PassportKey cookie does not match rampartjwt cookie");
		goto redirect;
	}
	
    return NGX_OK;
	
	redirect:
		r->headers_out.location = ngx_list_push(&r->headers_out.headers);
		if (r->headers_out.location == NULL) {
			ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
		}
		r->headers_out.location->hash = 1;
		r->headers_out.location->key.len = sizeof("Location") - 1;
		r->headers_out.location->key.data = (u_char *) "Location";
		r->headers_out.location->value.len = jwtcf->auth_jwt_loginurl.len;
		r->headers_out.location->value.data = jwtcf->auth_jwt_loginurl.data;
        return NGX_HTTP_MOVED_PERMANENTLY;
}


static ngx_int_t ngx_http_auth_jwt_init(ngx_conf_t *cf)
{
	ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_auth_jwt_handler;

    return NGX_OK;
}


static void *
ngx_http_auth_jwt_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_auth_jwt_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_auth_jwt_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }
	
	// set the flag to unset
	conf->auth_jwt_enabled = (ngx_flag_t) -1;

	ngx_conf_log_error(NGX_LOG_DEBUG, cf, 0, "Created Location Configuration");
	
    return conf;
}


static char *
ngx_http_auth_jwt_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_auth_jwt_loc_conf_t  *prev = parent;
    ngx_http_auth_jwt_loc_conf_t  *conf = child;

	ngx_conf_merge_str_value(conf->auth_jwt_loginurl, prev->auth_jwt_loginurl, "");
	ngx_conf_merge_str_value(conf->auth_jwt_key, prev->auth_jwt_key, "");
	
	
	if (conf->auth_jwt_enabled == ((ngx_flag_t) -1)) {
        conf->auth_jwt_enabled = (prev->auth_jwt_enabled == ((ngx_flag_t) -1)) ? 0 : prev->auth_jwt_enabled;
    }
	
	ngx_conf_log_error(NGX_LOG_DEBUG, cf, 0, "Merged Location Configuration");

//	ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "Key: %s, Enabled: %d", 
//			conf->auth_jwt_key.data, 
//			conf->auth_jwt_enabled);
    return NGX_CONF_OK;
}

static int
hex_char_to_binary( char ch, char* ret )
{
      ch = tolower( ch );      
      if( isdigit( ch ) )
            *ret = ch - '0';             
      else if( ch >= 'a' && ch <= 'f' )
            *ret = ( ch - 'a' ) + 10;
	  else if( ch >= 'A' && ch <= 'F' )
            *ret = ( ch - 'A' ) + 10;  
      else
            return *ret = 0;
      return 1;       
}

static int
hex_to_binary( const char* str, u_char* buf, int len ) {
	u_char	
        *cpy = buf;
	char
		low,
		high;
	int
		odd = len % 2;
	
	if (odd) {
		return -1;
	}

	for (int i = 0; i < len; i += 2) {
		hex_char_to_binary( *(str + i), &high );
		hex_char_to_binary( *(str + i + 1 ), &low );
		
		*cpy++ = low | (high << 4);
	}
	return 0;                              
}
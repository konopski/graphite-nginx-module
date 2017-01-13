#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

typedef struct {

    ngx_uint_t enable;

    ngx_str_t host;
    ngx_str_t protocol;
    ngx_shm_zone_t *shared;
    char *buffer;

    ngx_str_t prefix;

    struct in_addr server;
    int port;
    ngx_uint_t frequency;

    ngx_array_t *intervals;
    ngx_uint_t max_interval;
    ngx_array_t *splits;
    ngx_array_t *params;
    ngx_array_t *custom_params;
    ngx_array_t *custom_params_names;
    ngx_hash_t custom_params_hash;
    ngx_uint_t custom_params_hash_max_size;
    ngx_uint_t custom_params_hash_bucket_size;

    size_t shared_size;
    size_t buffer_size;
    size_t package_size;

    ngx_array_t *template;

    ngx_array_t *default_data;
    ngx_http_complex_value_t *default_data_filter;

    ngx_array_t *datas;

} ngx_http_graphite_main_conf_t;

typedef struct {
    ngx_array_t *default_data;
    ngx_http_complex_value_t *default_data_filter;

    ngx_array_t *datas;
} ngx_http_graphite_srv_conf_t;

typedef struct {
    ngx_array_t *datas;
} ngx_http_graphite_loc_conf_t;

#define SPLIT_EMPTY (ngx_uint_t)-1

static ngx_int_t ngx_http_graphite_add_variables(ngx_conf_t *cf);
static ngx_int_t ngx_http_graphite_init(ngx_conf_t *cf);
static ngx_int_t ngx_http_graphite_process_init(ngx_cycle_t *cycle);

static void *ngx_http_graphite_create_main_conf(ngx_conf_t *cf);
static void *ngx_http_graphite_create_srv_conf(ngx_conf_t *cf);
static void *ngx_http_graphite_create_loc_conf(ngx_conf_t *cf);

static ngx_str_t *ngx_http_graphite_split(ngx_pool_t *pool, ngx_str_t *uri);

#if (NGX_SSL)
static ngx_int_t ngx_http_graphite_ssl_session_reused(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data);
#endif

static char *ngx_http_graphite_config(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_graphite_param(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_graphite_default_data(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_graphite_data(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static ngx_int_t ngx_http_graphite_custom(ngx_http_request_t *r, ngx_str_t *name, double value);

static char *ngx_http_graphite_config_arg_prefix(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_host(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_server(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_port(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_frequency(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_intervals(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_params(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_shared(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_buffer(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_package(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_template(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);
static char *ngx_http_graphite_config_arg_protocol(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value);


static char *ngx_http_graphite_param_arg_name(ngx_conf_t *cf, ngx_command_t *cmd, void *data, ngx_str_t *value);
static char *ngx_http_graphite_param_arg_aggregate(ngx_conf_t *cf, ngx_command_t *cmd, void *data, ngx_str_t *value);
static char *ngx_http_graphite_param_arg_interval(ngx_conf_t *cf, ngx_command_t *cmd, void *data, ngx_str_t *value);

static char *ngx_http_graphite_add_default_data(ngx_conf_t *cf, ngx_array_t *splits, ngx_array_t *datas, ngx_str_t *split, ngx_array_t *template, ngx_http_complex_value_t *filter);
static char *ngx_http_graphite_add_data(ngx_conf_t *cf, ngx_array_t *splits, ngx_array_t *datas, ngx_str_t *name, ngx_http_complex_value_t *filter);

static char *ngx_http_graphite_parse_string(ngx_conf_t *cf, ngx_str_t *value, ngx_str_t *result);
static char *ngx_http_graphite_parse_size(ngx_str_t *value, size_t *result);
static char *ngx_http_graphite_parse_time(ngx_str_t *value, ngx_uint_t *result);

static double ngx_http_graphite_param_request_time(ngx_http_request_t *r);
static double ngx_http_graphite_param_bytes_sent(ngx_http_request_t *r);
static double ngx_http_graphite_param_body_bytes_sent(ngx_http_request_t *r);
static double ngx_http_graphite_param_request_length(ngx_http_request_t *r);
static double ngx_http_graphite_param_ssl_handshake_time(ngx_http_request_t *r);
static double ngx_http_graphite_param_ssl_cache_usage(ngx_http_request_t *r);
static double ngx_http_graphite_param_content_time(ngx_http_request_t *r);
static double ngx_http_graphite_param_gzip_time(ngx_http_request_t *r);
static double ngx_http_graphite_param_upstream_time(ngx_http_request_t *r);
#if nginx_version >= 1009001
static double ngx_http_graphite_param_upstream_connect_time(ngx_http_request_t *r);
static double ngx_http_graphite_param_upstream_header_time(ngx_http_request_t *r);
#endif
static double ngx_http_graphite_param_rps(ngx_http_request_t *r);
static double ngx_http_graphite_param_keepalive_rps(ngx_http_request_t *r);
static double ngx_http_graphite_param_response_2xx_rps(ngx_http_request_t *r);
static double ngx_http_graphite_param_response_3xx_rps(ngx_http_request_t *r);
static double ngx_http_graphite_param_response_4xx_rps(ngx_http_request_t *r);
static double ngx_http_graphite_param_response_5xx_rps(ngx_http_request_t *r);

static ngx_command_t ngx_http_graphite_commands[] = {

    { ngx_string("graphite_config"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_ANY,
      ngx_http_graphite_config,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("graphite_param_hash_max_size"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_graphite_main_conf_t, custom_params_hash_max_size),
      NULL },

    { ngx_string("graphite_param_hash_bucket_size"),
      NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_graphite_main_conf_t, custom_params_hash_bucket_size),
      NULL },

    { ngx_string("graphite_param"),
      NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE3,
      ngx_http_graphite_param,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("graphite_default_data"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_CONF_TAKE12,
      ngx_http_graphite_default_data,
      0,
      0,
      NULL },

    { ngx_string("graphite_data"),
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_HTTP_LIF_CONF | NGX_CONF_TAKE12,
      ngx_http_graphite_data,
      0,
      0,
      NULL },

   { ngx_string("graphite_custom"),
      0,
      NULL,
      0,
      0,
      ngx_http_graphite_custom },

      ngx_null_command
};

static ngx_http_module_t ngx_http_graphite_module_ctx = {
    ngx_http_graphite_add_variables,       /* preconfiguration */
    ngx_http_graphite_init,                /* postconfiguration */

    ngx_http_graphite_create_main_conf,    /* create main configuration */
    NULL,                                  /* init main configuration */

    ngx_http_graphite_create_srv_conf,     /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_graphite_create_loc_conf,     /* create location configuration */
    NULL                                   /* merge location configuration */
};

ngx_module_t ngx_http_graphite_module = {
    NGX_MODULE_V1,
    &ngx_http_graphite_module_ctx,         /* module context */
    ngx_http_graphite_commands,            /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_http_graphite_process_init,        /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

static ngx_str_t graphite_shared_name = ngx_string("graphite_shared");

static ngx_http_variable_t ngx_http_graphite_vars[] = {

#if (NGX_SSL)
    { ngx_string("ssl_session_reused"), NULL, ngx_http_graphite_ssl_session_reused, 0, NGX_HTTP_VAR_CHANGEABLE, 0 },
#endif

    { ngx_null_string, NULL, NULL, 0, 0, 0 }
};

typedef char *(*ngx_http_graphite_arg_handler_pt)(ngx_conf_t*, ngx_command_t*, void *, ngx_str_t *);

typedef struct ngx_http_graphite_arg_s {
    ngx_str_t name;
    ngx_http_graphite_arg_handler_pt handler;
    ngx_str_t deflt;
} ngx_http_graphite_arg_t;

#define CONFIG_ARGS_COUNT 12

static const ngx_http_graphite_arg_t ngx_http_graphite_config_args[CONFIG_ARGS_COUNT] = {
    { ngx_string("prefix"), ngx_http_graphite_config_arg_prefix, ngx_null_string },
    { ngx_string("host"), ngx_http_graphite_config_arg_host, ngx_null_string },
    { ngx_string("server"), ngx_http_graphite_config_arg_server, ngx_null_string },
    { ngx_string("port"), ngx_http_graphite_config_arg_port, ngx_string("2003") },
    { ngx_string("frequency"), ngx_http_graphite_config_arg_frequency, ngx_string("60") },
    { ngx_string("intervals"), ngx_http_graphite_config_arg_intervals, ngx_string("1m") },
#if nginx_version >= 1009001
    { ngx_string("params"), ngx_http_graphite_config_arg_params, ngx_string("request_time|bytes_sent|body_bytes_sent|request_length|ssl_handshake_time|ssl_cache_usage|content_time|gzip_time|upstream_time|upstream_connect_time|upstream_header_time|rps|keepalive_rps|response_2xx_rps|response_3xx_rps|response_4xx_rps|response_5xx_rps") },
#else
    { ngx_string("params"), ngx_http_graphite_config_arg_params, ngx_string("request_time|bytes_sent|body_bytes_sent|request_length|ssl_handshake_time|ssl_cache_usage|content_time|gzip_time|upstream_time|rps|keepalive_rps|response_2xx_rps|response_3xx_rps|response_4xx_rps|response_5xx_rps") },
#endif
    { ngx_string("shared"), ngx_http_graphite_config_arg_shared, ngx_string("1m") },
    { ngx_string("buffer"), ngx_http_graphite_config_arg_buffer, ngx_string("64k") },
    { ngx_string("package"), ngx_http_graphite_config_arg_package, ngx_string("1400") },
    { ngx_string("template"), ngx_http_graphite_config_arg_template, ngx_null_string },
    { ngx_string("protocol"), ngx_http_graphite_config_arg_protocol, ngx_string("udp") },
};

#define PARAM_ARGS_COUNT 3

static const ngx_http_graphite_arg_t ngx_http_graphite_param_args[PARAM_ARGS_COUNT] = {
    { ngx_string("name"), ngx_http_graphite_param_arg_name, ngx_null_string },
    { ngx_string("aggregate"), ngx_http_graphite_param_arg_aggregate, ngx_null_string },
    { ngx_string("interval"), ngx_http_graphite_param_arg_interval, ngx_null_string },
};

static ngx_event_t timer_event;

typedef struct ngx_http_graphite_acc_s {
    double value;
    ngx_uint_t count;
} ngx_http_graphite_acc_t;

typedef struct ngx_http_graphite_storage_s {
    ngx_http_graphite_acc_t *accs;
    ngx_http_graphite_acc_t *custom_accs;

    time_t start_time;
    time_t last_time;
    time_t event_time;

} ngx_http_graphite_storage_t;

typedef struct ngx_http_graphite_interval_s {
    ngx_str_t name;
    ngx_uint_t value;
} ngx_http_graphite_interval_t;

typedef double (*ngx_http_graphite_aggregate_pt)(const ngx_http_graphite_interval_t*, const ngx_http_graphite_acc_t*);

static double ngx_http_graphite_aggregate_avg(const ngx_http_graphite_interval_t *interval, const ngx_http_graphite_acc_t *acc);
static double ngx_http_graphite_aggregate_persec(const ngx_http_graphite_interval_t *interval, const ngx_http_graphite_acc_t *acc);
static double ngx_http_graphite_aggregate_sum(const ngx_http_graphite_interval_t *interval, const ngx_http_graphite_acc_t *acc);

typedef struct ngx_http_graphite_aggregate_s {
    ngx_str_t name;
    ngx_http_graphite_aggregate_pt get;
} ngx_http_graphite_aggregate_t;

#define AGGREGATE_COUNT 3

static const ngx_http_graphite_aggregate_t ngx_http_graphite_aggregates[AGGREGATE_COUNT] = {
    { ngx_string("avg"), ngx_http_graphite_aggregate_avg },
    { ngx_string("persec"), ngx_http_graphite_aggregate_persec },
    { ngx_string("sum"), ngx_http_graphite_aggregate_sum },
};

typedef double (*ngx_http_graphite_param_handler_pt)(ngx_http_request_t*);

typedef struct ngx_http_graphite_param_s {
    ngx_str_t name;
    ngx_http_graphite_param_handler_pt get;
    ngx_http_graphite_aggregate_pt aggregate;
    ngx_http_graphite_interval_t interval;
} ngx_http_graphite_param_t;

#if nginx_version >= 1009001
#define PARAM_COUNT 17
#else
#define PARAM_COUNT 15
#endif

static const ngx_http_graphite_param_t ngx_http_graphite_params[PARAM_COUNT] = {
    { ngx_string("request_time"), ngx_http_graphite_param_request_time, ngx_http_graphite_aggregate_avg, { ngx_null_string, 0 } },
    { ngx_string("bytes_sent"), ngx_http_graphite_param_bytes_sent, ngx_http_graphite_aggregate_avg, { ngx_null_string, 0 } },
    { ngx_string("body_bytes_sent"), ngx_http_graphite_param_body_bytes_sent, ngx_http_graphite_aggregate_avg, { ngx_null_string, 0 } },
    { ngx_string("request_length"), ngx_http_graphite_param_request_length, ngx_http_graphite_aggregate_avg, { ngx_null_string, 0 } },
    { ngx_string("ssl_handshake_time"), ngx_http_graphite_param_ssl_handshake_time, ngx_http_graphite_aggregate_avg, { ngx_null_string, 0 } },
    { ngx_string("ssl_cache_usage"), ngx_http_graphite_param_ssl_cache_usage, ngx_http_graphite_aggregate_avg, { ngx_null_string, 0 } },
    { ngx_string("content_time"), ngx_http_graphite_param_content_time, ngx_http_graphite_aggregate_avg, { ngx_null_string, 0 } },
    { ngx_string("gzip_time"), ngx_http_graphite_param_gzip_time, ngx_http_graphite_aggregate_avg, { ngx_null_string, 0 } },
    { ngx_string("upstream_time"), ngx_http_graphite_param_upstream_time, ngx_http_graphite_aggregate_avg, { ngx_null_string, 0 } },
#if nginx_version >= 1009001
    { ngx_string("upstream_connect_time"), ngx_http_graphite_param_upstream_connect_time, ngx_http_graphite_aggregate_avg, { ngx_null_string, 0 } },
    { ngx_string("upstream_header_time"), ngx_http_graphite_param_upstream_header_time, ngx_http_graphite_aggregate_avg, { ngx_null_string, 0 } },
#endif
    { ngx_string("rps"), ngx_http_graphite_param_rps, ngx_http_graphite_aggregate_persec, { ngx_null_string, 0 } },
    { ngx_string("keepalive_rps"), ngx_http_graphite_param_keepalive_rps, ngx_http_graphite_aggregate_persec, { ngx_null_string, 0 } },
    { ngx_string("response_2xx_rps"), ngx_http_graphite_param_response_2xx_rps, ngx_http_graphite_aggregate_persec, { ngx_null_string, 0 } },
    { ngx_string("response_3xx_rps"), ngx_http_graphite_param_response_3xx_rps, ngx_http_graphite_aggregate_persec, { ngx_null_string, 0 } },
    { ngx_string("response_4xx_rps"), ngx_http_graphite_param_response_4xx_rps, ngx_http_graphite_aggregate_persec, { ngx_null_string, 0 } },
    { ngx_string("response_5xx_rps"), ngx_http_graphite_param_response_5xx_rps, ngx_http_graphite_aggregate_persec, { ngx_null_string, 0 } },
};

typedef struct ngx_http_graphite_data_t {
    ngx_uint_t split;
    ngx_http_complex_value_t *filter;
} ngx_http_graphite_data_t;

typedef struct ngx_http_graphite_template_arg_s {
    ngx_str_t name;
    int variable;
} ngx_http_graphite_template_arg_t;

typedef struct ngx_http_graphite_template_s {
    ngx_str_t data;
    int variable;
} ngx_http_graphite_template_t;

static char *ngx_http_graphite_template_compile(ngx_conf_t *cf, ngx_array_t *template, const ngx_http_graphite_template_arg_t *args, size_t nargs, const ngx_str_t *value);
static char *ngx_http_graphite_template_execute(char* buffer, size_t buffer_size, const ngx_array_t *template, const ngx_str_t *variables[]);
static size_t ngx_http_graphite_template_len(const ngx_array_t *template, const ngx_str_t *variables[]);

typedef enum {
    TEMPLATE_VARIABLE_PREFIX,
    TEMPLATE_VARIABLE_HOST,
    TEMPLATE_VARIABLE_SPLIT,
    TEMPLATE_VARIABLE_PARAM,
    TEMPLATE_VARIABLE_INTERVAL,
} ngx_http_graphite_template_variable_t;

#define TEMPLATE_ARG_COUNT 5

static const ngx_http_graphite_template_arg_t ngx_http_graphite_template_args[TEMPLATE_ARG_COUNT] = {
    { ngx_string("prefix"), TEMPLATE_VARIABLE_PREFIX },
    { ngx_string("host"), TEMPLATE_VARIABLE_HOST },
    { ngx_string("split"), TEMPLATE_VARIABLE_SPLIT },
    { ngx_string("param"), TEMPLATE_VARIABLE_PARAM },
    { ngx_string("interval"), TEMPLATE_VARIABLE_INTERVAL },
};

#define TEMPLATE_VARIABLES(prefix, host, split, param, interval) {prefix, host, split, param, interval}

typedef enum {
    TEMPLATE_VARIABLE_LOCATION,
} ngx_http_graphite_split_variable_t;

#define SPLIT_ARG_COUNT 1

static const ngx_http_graphite_template_arg_t ngx_http_graphite_split_args[SPLIT_ARG_COUNT] = {
    { ngx_string("location"), TEMPLATE_VARIABLE_LOCATION },
};

#define SPLIT_VARIABLES(location) {location}

static ngx_http_complex_value_t *ngx_http_graphite_complex_compile(ngx_conf_t *cf, ngx_str_t *value);

static ngx_int_t ngx_http_graphite_shared_init(ngx_shm_zone_t *shm_zone, void *data);
ngx_int_t ngx_http_graphite_handler(ngx_http_request_t *r);
static void ngx_http_graphite_timer_event_handler(ngx_event_t *ev);
void ngx_http_graphite_del_old_records(ngx_http_graphite_main_conf_t *gmcf, time_t ts);
static double *ngx_http_graphite_get_params(ngx_http_request_t *r);

static ngx_int_t
ngx_http_graphite_add_variables(ngx_conf_t *cf)
{
    ngx_http_variable_t *var, *v;

    for (v = ngx_http_graphite_vars; v->name.len; v++) {
        var = ngx_http_add_variable(cf, &v->name, v->flags);
        if (var == NULL) {
            return NGX_ERROR;
        }

        var->get_handler = v->get_handler;
        var->data = v->data;
    }

    return NGX_OK;
}

static ngx_int_t
ngx_http_graphite_init(ngx_conf_t *cf) {

    ngx_http_handler_pt *h;
    ngx_http_core_main_conf_t *cmcf;
    ngx_http_graphite_main_conf_t *gmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    ngx_hash_init_t hash;
    hash.hash = &gmcf->custom_params_hash;
    hash.key = ngx_hash_key_lc;
    hash.max_size = gmcf->custom_params_hash_max_size != (ngx_uint_t)NGX_CONF_UNSET ? gmcf->custom_params_hash_max_size : 512;
    hash.bucket_size = gmcf->custom_params_hash_bucket_size != (ngx_uint_t)NGX_CONF_UNSET ? gmcf->custom_params_hash_bucket_size : ngx_align(64, ngx_cacheline_size);
    hash.name = "graphite_param_hash";
    hash.pool = cf->pool;
    hash.temp_pool = NULL;

    if (ngx_hash_init(&hash, gmcf->custom_params_names->elts, gmcf->custom_params_names->nelts) != NGX_OK) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't init custom params hash");
        return NGX_ERROR;
    }

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_LOG_PHASE].handlers);
    if (h == NULL)
        return NGX_ERROR;

    *h = ngx_http_graphite_handler;

    return NGX_OK;
}

static ngx_int_t
ngx_http_graphite_process_init(ngx_cycle_t *cycle) {

    ngx_http_graphite_main_conf_t *gmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_graphite_module);

    if (gmcf->enable) {

        ngx_memzero(&timer_event, sizeof(timer_event));
        timer_event.handler = ngx_http_graphite_timer_event_handler;
        timer_event.data = gmcf;
        timer_event.log = cycle->log;
        ngx_add_timer(&timer_event, gmcf->frequency);
    }

    return NGX_OK;
}

static void *
ngx_http_graphite_create_main_conf(ngx_conf_t *cf) {

    ngx_http_graphite_main_conf_t *gmcf;
    gmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_graphite_main_conf_t));

    if (gmcf == NULL)
        return NULL;

    gmcf->splits = ngx_array_create(cf->pool, 1, sizeof(ngx_str_t));
    gmcf->intervals = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_interval_t));
    gmcf->params = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_param_t));
    gmcf->custom_params = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_param_t));
    gmcf->custom_params_names = ngx_array_create(cf->pool, 1, sizeof(ngx_hash_key_t));
    gmcf->template = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_template_t));
    gmcf->default_data = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_template_t));
    gmcf->datas = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_data_t));

    if (gmcf->splits == NULL || gmcf->intervals == NULL || gmcf->params == NULL || gmcf->custom_params == NULL || gmcf->custom_params_names == NULL || gmcf->template == NULL || gmcf->default_data == NULL || gmcf->datas == NULL) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NULL;
    }

    gmcf->custom_params_hash_max_size = NGX_CONF_UNSET;
    gmcf->custom_params_hash_bucket_size = NGX_CONF_UNSET;

    return gmcf;
}

static void *
ngx_http_graphite_create_srv_conf(ngx_conf_t *cf) {

    ngx_http_graphite_srv_conf_t *gscf;
    gscf = ngx_pcalloc(cf->pool, sizeof(ngx_http_graphite_srv_conf_t));

    if (gscf == NULL)
        return NULL;

    gscf->default_data = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_template_t));
    gscf->datas = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_data_t));

    if (gscf->default_data == NULL || gscf->datas == NULL) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NULL;
    }

    return gscf;
}

static void *
ngx_http_graphite_create_loc_conf(ngx_conf_t *cf) {

    ngx_http_graphite_main_conf_t *gmcf;
    ngx_http_graphite_srv_conf_t *gscf;
    ngx_http_graphite_loc_conf_t *glcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);
    glcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_graphite_loc_conf_t));

    if (glcf == NULL)
        return NULL;

    glcf->datas = ngx_array_create(cf->pool, 1, sizeof(ngx_http_graphite_data_t));
    if (glcf->datas == NULL) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NULL;
    }

    if (!cf->args)
        return glcf;

    ngx_str_t *directive = &((ngx_str_t*)cf->args->elts)[0];
    ngx_str_t location = ngx_string("location");

    if (cf->args->nelts >= 2 && directive->len == location.len && ngx_strncmp(directive->data, location.data, location.len) == 0) {
        gscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_graphite_module);

        ngx_str_t *uri = &((ngx_str_t*)cf->args->elts)[cf->args->nelts - 1];
        ngx_str_t *split = ngx_http_graphite_split(cf->pool, uri);
        if (split == NULL)
            return NULL;

        if (split->len == 0)
            return glcf;

        if (ngx_http_graphite_add_default_data(cf, gmcf->splits, glcf->datas, split, gmcf->default_data, gmcf->default_data_filter) != NGX_CONF_OK)
            return NULL;

        if (ngx_http_graphite_add_default_data(cf, gmcf->splits, glcf->datas, split, gscf->default_data, gscf->default_data_filter) != NGX_CONF_OK)
            return NULL;
    }

    return glcf;
}

static ngx_str_t *
ngx_http_graphite_split(ngx_pool_t *pool, ngx_str_t *uri) {

    ngx_str_t *split = ngx_palloc(pool, sizeof(ngx_str_t));
    if (!split)
        return NULL;

    split->data = ngx_palloc(pool, uri->len);
    split->len = 0;

    if (!split->data)
        return NULL;

    size_t i;
    for (i = 0; i < uri->len; i++) {
        if (isalnum(uri->data[i]))
            split->data[split->len++] = uri->data[i];
        else
            split->data[split->len++] = '_';
    }

    while (split->len > 0 && split->data[0] == '_') {
        split->data++;
        split->len--;
    }

    while (split->len > 0 && split->data[split->len - 1] == '_')
        split->len--;

    return split;
}

#if (NGX_SSL)
static ngx_int_t
ngx_http_graphite_ssl_session_reused(ngx_http_request_t *r, ngx_http_variable_value_t *v, uintptr_t data) {

    ngx_str_t s;

    if (r->connection->requests == 1) {
        if (r->connection->ssl && SSL_session_reused(r->connection->ssl->connection)) {
            ngx_str_set(&s, "yes");
        }
        else {
            ngx_str_set(&s, "no");
        }
    }
    else {
        ngx_str_set(&s, "none");
    }

    v->len = s.len;
    v->data = s.data;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;

    return NGX_OK;
}
#endif

#define HOST_LEN 256

static char *
ngx_http_graphite_parse_args(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, const ngx_http_graphite_arg_t *args, ngx_uint_t args_count) {

    ngx_uint_t isset[args_count];
    ngx_memzero(isset, args_count * sizeof(ngx_uint_t));

    ngx_uint_t i;
    for (i = 1; i < cf->args->nelts; i++) {
        ngx_str_t *var = &((ngx_str_t*)cf->args->elts)[i];

        ngx_uint_t find = 0;
        ngx_uint_t j;
        for (j = 0; j < args_count; j++) {
            const ngx_http_graphite_arg_t *arg = &args[j];

            if (!ngx_strncmp(arg->name.data, var->data, arg->name.len) && var->data[arg->name.len] == '=') {
                isset[j] = 1;
                find = 1;
                ngx_str_t value;
                value.data = var->data + arg->name.len + 1;
                value.len = var->len - (arg->name.len + 1);

                if (arg->handler(cf, cmd, conf, &value) == NGX_CONF_ERROR) {
                    ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite invalid option value %V", var);
                    return NGX_CONF_ERROR;
                }
                break;
            }
        }

        if (!find) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite unknown option %V", var);
            return NGX_CONF_ERROR;
        }
    }

    for (i = 0; i < args_count; i++) {
        if (!isset[i]) {

            const ngx_http_graphite_arg_t *arg = &args[i];
            if (arg->deflt.len) {
                if (arg->handler(cf, cmd, conf, (ngx_str_t*)&arg->deflt) == NGX_CONF_ERROR) {
                      ngx_conf_log_error(NGX_LOG_CRIT, cf, 0, "graphite invalid option default value %V", &arg->deflt);
                      return NGX_CONF_ERROR;
                }
            }
        }
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    if (ngx_http_graphite_parse_args(cf, cmd, conf, ngx_http_graphite_config_args, CONFIG_ARGS_COUNT) == NGX_CONF_ERROR)
        return NGX_CONF_ERROR;

    ngx_http_graphite_main_conf_t *gmcf = conf;

    if (gmcf->host.len == 0) {
        char host[HOST_LEN];
        gethostname(host, HOST_LEN);
        host[HOST_LEN - 1] = '\0';
        char *dot = strchr(host, '.');
        if (dot)
            *dot = '\0';

        size_t host_size = strlen(host);

        gmcf->host.data = ngx_palloc(cf->pool, host_size);
        if (!gmcf->host.data) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }

        ngx_memcpy(gmcf->host.data, host, host_size);
        gmcf->host.len = host_size;
    }

    if (gmcf->server.s_addr == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config server not set");
        return NGX_CONF_ERROR;
    }
    if (gmcf->protocol.len == 0){
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config protocol is not specified");
        return NGX_CONF_ERROR;
    } else if (ngx_strcmp(gmcf->protocol.data, "udp") != 0 && ngx_strcmp(gmcf->protocol.data, "tcp") != 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config invalid protocol");
        return NGX_CONF_ERROR;
    }

    if (gmcf->port < 1 || gmcf->port > 65535) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config port must be in range form 1 to 65535");
        return NGX_CONF_ERROR;
    }

    if (gmcf->frequency < 1 || gmcf->frequency > 65535) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config frequency must be in range form 1 to 65535");
        return NGX_CONF_ERROR;
    }

    if (gmcf->intervals->nelts == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config intervals not set");
        return NGX_CONF_ERROR;
    }

    if (gmcf->params->nelts == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config params not set");
        return NGX_CONF_ERROR;
    }

    if (gmcf->shared_size == 0 || gmcf->buffer_size == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config shared must be positive value");
        return NGX_CONF_ERROR;
    }

    if (gmcf->buffer_size == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config buffer must be positive value");
        return NGX_CONF_ERROR;
    }

    if (gmcf->package_size == 0) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config package must be positive value");
        return NGX_CONF_ERROR;
    }

    if (gmcf->shared_size < sizeof(ngx_slab_pool_t)) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite too small shared memory");
        return NGX_CONF_ERROR;
    }

    ngx_str_t graphite_shared_id;
    graphite_shared_id.len = graphite_shared_name.len + 32;
    graphite_shared_id.data = ngx_palloc(cf->pool, graphite_shared_id.len);
    if (!graphite_shared_id.data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }
    ngx_snprintf(graphite_shared_id.data, graphite_shared_id.len, "%V.%T", &graphite_shared_name, ngx_time());

    gmcf->shared = ngx_shared_memory_add(cf, &graphite_shared_id, gmcf->shared_size, &ngx_http_graphite_module);
    if (!gmcf->shared) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc shared memory");
        return NGX_CONF_ERROR;
    }
    if (gmcf->shared->data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite shared memory is used");
        return NGX_CONF_ERROR;
    }
    gmcf->shared->init = ngx_http_graphite_shared_init;
    gmcf->shared->data = gmcf;

    gmcf->buffer = ngx_palloc(cf->pool, gmcf->buffer_size);
    if (!gmcf->buffer) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }

    gmcf->enable = 1;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_param(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    if (!gmcf->enable) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config not set");
        return NGX_CONF_ERROR;
    }

    ngx_http_graphite_param_t data;
    ngx_memzero(&data, sizeof(ngx_http_graphite_param_t));
    if (ngx_http_graphite_parse_args(cf, cmd, (void*)&data, ngx_http_graphite_param_args, PARAM_ARGS_COUNT) == NGX_CONF_ERROR)
        return NGX_CONF_ERROR;

    if (!data.name.data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param name not set");
        return NGX_CONF_ERROR;
    }

    if (!data.aggregate) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param aggregate not set");
        return NGX_CONF_ERROR;
    }

    if (!data.interval.name.data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param interval not set");
        return NGX_CONF_ERROR;
    }

    if (data.interval.value > gmcf->max_interval) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param interval value is greather than max interval");
        return NGX_CONF_ERROR;
    }

    ngx_uint_t p;
    for (p = 0; p < gmcf->custom_params->nelts; p++) {
        ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->custom_params->elts)[p];
        if (!ngx_strncmp(param->name.data, data.name.data, data.name.len)) {
            if (param->aggregate != data.aggregate) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param is already defined with a different aggregate");
                return NGX_CONF_ERROR;
            }
            if (param->interval.value != data.interval.value) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param is already defined with a different interval");
                return NGX_CONF_ERROR;
            }
            return NGX_CONF_OK;
        }
    }

    p = gmcf->custom_params->nelts;
    ngx_http_graphite_param_t *param = ngx_array_push(gmcf->custom_params);
    if (!param) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }

    ngx_memcpy(param, &data, sizeof(ngx_http_graphite_param_t));

    ngx_hash_key_t *hk = ngx_array_push(gmcf->custom_params_names);
    if (hk == NULL) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }

    hk->key = param->name;
    hk->key_hash = ngx_hash_key_lc(param->name.data, param->name.len);
    hk->value = (void*)(p + 1);

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_default_data(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

    ngx_http_graphite_main_conf_t *gmcf;
    ngx_http_graphite_srv_conf_t *gscf;

    gscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_graphite_module);
    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    if (!gmcf->enable) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config not set");
        return NGX_CONF_ERROR;
    }

    ngx_str_t *value = cf->args->elts;

    ngx_array_t *default_data = NULL;
    ngx_http_complex_value_t **filter;

    if (cf->cmd_type == NGX_HTTP_MAIN_CONF) {
        default_data = gmcf->default_data;
        filter = &gmcf->default_data_filter;
    }
    else if (cf->cmd_type == NGX_HTTP_SRV_CONF) {
        default_data = gscf->default_data;
        filter = &gscf->default_data_filter;
    }
    else
        return NGX_CONF_ERROR;

    ngx_str_t *template = &value[1];
    if (ngx_http_graphite_template_compile(cf, default_data, ngx_http_graphite_split_args, SPLIT_ARG_COUNT, template) != NGX_CONF_OK)
        return NGX_CONF_ERROR;

    if (cf->args->nelts >= 3) {
        ngx_uint_t i;
        for (i = 2; i < cf->args->nelts; i++) {
            if (ngx_strncmp(value[i].data, "if=", 3) == 0) {
                ngx_str_t condition;
                condition.len = value[i].len - 3;
                condition.data = value[i].data + 3;

                *filter = ngx_http_graphite_complex_compile(cf, &condition);
                if (!*filter)
                    return NGX_CONF_ERROR;
            }
            else {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite unknown option %V", &value[i]);
                return NGX_CONF_ERROR;
            }
        }
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_data(ngx_conf_t *cf, ngx_command_t *cmd, void *conf) {

    ngx_http_graphite_main_conf_t *gmcf;
    ngx_http_graphite_srv_conf_t *gscf;
    ngx_http_graphite_loc_conf_t *glcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);
    gscf = ngx_http_conf_get_module_srv_conf(cf, ngx_http_graphite_module);
    glcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_graphite_module);

    if (!gmcf->enable) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config not set");
        return NGX_CONF_ERROR;
    }

    ngx_str_t *value = cf->args->elts;

    ngx_str_t *name = &value[1];
    ngx_http_complex_value_t *filter = NULL;

    if (cf->args->nelts >= 3) {
        ngx_uint_t i;
        for (i = 2; i < cf->args->nelts; i++) {
            if (ngx_strncmp(value[i].data, "if=", 3) == 0) {
                ngx_str_t condition;
                condition.len = value[i].len - 3;
                condition.data = value[i].data + 3;

                filter = ngx_http_graphite_complex_compile(cf, &condition);
                if (!filter)
                    return NGX_CONF_ERROR;
            }
            else {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite unknown option %V", &value[i]);
                return NGX_CONF_ERROR;
            }
        }
    }

    ngx_array_t *datas;

    if (cf->cmd_type & NGX_HTTP_MAIN_CONF)
        datas = gmcf->datas;
    else if (cf->cmd_type & NGX_HTTP_SRV_CONF)
        datas = gscf->datas;
    else
        datas = glcf->datas;

    if (ngx_http_graphite_add_data(cf, gmcf->splits, datas, name, filter) != NGX_CONF_OK)
        return NGX_CONF_ERROR;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_prefix(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    return ngx_http_graphite_parse_string(cf, value, &gmcf->prefix);
}

static char *
ngx_http_graphite_config_arg_host(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    return ngx_http_graphite_parse_string(cf, value, &gmcf->host);
}

static char *
ngx_http_graphite_config_arg_protocol(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_graphite_module);

    return ngx_http_graphite_parse_string(cf, value, &gmcf->protocol);
}

#define SERVER_LEN 255

static char *
ngx_http_graphite_config_arg_server(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;

    char server[SERVER_LEN];
    if (value->len >= SERVER_LEN) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite server name too long");
        return NGX_CONF_ERROR;
    }

    ngx_memcpy((u_char*)server, value->data, value->len);
    server[value->len] = '\0';

    in_addr_t a = inet_addr(server);
    if (a != (in_addr_t)-1 || !a)
        gmcf->server.s_addr = a;
    else
    {
        struct hostent *host = gethostbyname(server);
        if (host != NULL) {
            gmcf->server = *(struct in_addr*)*host->h_addr_list;
        }
        else {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't resolve server name");
            return NGX_CONF_ERROR;
        }
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_port(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    gmcf->port = ngx_atoi(value->data, value->len);

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_frequency(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    gmcf->frequency = ngx_atoi(value->data, value->len) * 1000;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_intervals(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;

    ngx_uint_t i;
    ngx_uint_t s = 0;
    for (i = 0; i <= value->len; i++) {

        if (i == value->len || value->data[i] == '|') {

            if (i == s) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config intervals is empty");
                return NGX_CONF_ERROR;
            }

            ngx_http_graphite_interval_t *interval = ngx_array_push(gmcf->intervals);
            if (!interval) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
                return NGX_CONF_ERROR;
            }

            interval->name.data = ngx_palloc(cf->pool, i - s);
            if (!interval->name.data) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
                return NGX_CONF_ERROR;
            }

            ngx_memcpy(interval->name.data, &value->data[s], i - s);
            interval->name.len = i - s;

            if (ngx_http_graphite_parse_time(&interval->name, &interval->value) == NGX_CONF_ERROR) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite config interval is invalid");
                return NGX_CONF_ERROR;
            }

            if (interval->value > gmcf->max_interval)
                gmcf->max_interval = interval->value;

            s = i + 1;
        }
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_params(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;

    ngx_uint_t i = 0;
    ngx_uint_t s = 0;
    for (i = 0; i <= value->len; i++) {
        if (i == value->len || value->data[i] == '|') {

            if (i == s) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite params is empty");
                return NGX_CONF_ERROR;
            }

            ngx_http_graphite_param_t *param = ngx_array_push(gmcf->params);
            if (!param) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
                return NGX_CONF_ERROR;
            }

            ngx_uint_t find = 0;
            ngx_uint_t p;
            for (p = 0; p < PARAM_COUNT; p++) {
                if ((ngx_http_graphite_params[p].name.len == i - s) && !ngx_strncmp(ngx_http_graphite_params[p].name.data, &value->data[s], i - s)) {
                    find = 1;
                    *param = ngx_http_graphite_params[p];
                }
            }

            if (!find) {
                ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite unknow param %*s", i - s, &value->data[s]);
                return NGX_CONF_ERROR;
            }

            s = i + 1;
        }
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_config_arg_shared(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    return ngx_http_graphite_parse_size(value, &gmcf->shared_size);
}

static char *
ngx_http_graphite_config_arg_buffer(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    return ngx_http_graphite_parse_size(value, &gmcf->buffer_size);
}

static char *
ngx_http_graphite_config_arg_package(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    return ngx_http_graphite_parse_size(value, &gmcf->package_size);
}

static char *
ngx_http_graphite_config_arg_template(ngx_conf_t *cf, ngx_command_t *cmd, void *conf, ngx_str_t *value) {

    ngx_http_graphite_main_conf_t *gmcf = conf;
    return ngx_http_graphite_template_compile(cf, gmcf->template, ngx_http_graphite_template_args, TEMPLATE_ARG_COUNT, value);
}

static char *
ngx_http_graphite_param_arg_name(ngx_conf_t *cf, ngx_command_t *cmd, void *data, ngx_str_t *value) {

    ngx_http_graphite_param_t *param = (ngx_http_graphite_param_t*)data;
    param->name.data = ngx_pcalloc(cf->pool, value->len);
    if (!param->name.data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }
    ngx_memcpy(param->name.data, value->data, value->len);
    param->name.len = value->len;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_param_arg_aggregate(ngx_conf_t *cf, ngx_command_t *cmd, void *data, ngx_str_t *value) {

    ngx_http_graphite_param_t *param = (ngx_http_graphite_param_t*)data;

    ngx_uint_t find = 0;
    ngx_uint_t a;
    for (a = 0; a < AGGREGATE_COUNT; a++) {
        if ((ngx_http_graphite_aggregates[a].name.len == value->len) && !ngx_strncmp(ngx_http_graphite_aggregates[a].name.data, value->data, value->len)) {
            find = 1;
            param->aggregate = ngx_http_graphite_aggregates[a].get;
        }
    }

    if (!find) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param unknow aggregate %V", value);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_param_arg_interval(ngx_conf_t *cf, ngx_command_t *cmd, void *data, ngx_str_t *value) {

    ngx_http_graphite_param_t *param = (ngx_http_graphite_param_t*)data;
    ngx_http_graphite_interval_t *interval = &param->interval;

    interval->name.data = ngx_palloc(cf->pool, value->len);
    if (!interval->name.data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }

    ngx_memcpy(interval->name.data, value->data, value->len);
    interval->name.len = value->len;

    if (ngx_http_graphite_parse_time(value, &interval->value) == NGX_CONF_ERROR) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite param interval is invalid");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_add_default_data(ngx_conf_t *cf, ngx_array_t *splits, ngx_array_t *datas, ngx_str_t *split, ngx_array_t *template, ngx_http_complex_value_t *filter) {

    if (template->nelts == 0)
        return NGX_CONF_OK;

    const ngx_str_t *variables[] = SPLIT_VARIABLES(split);

    ngx_str_t name;
    name.len = ngx_http_graphite_template_len(template, variables);
    if (name.len == 0)
        return NGX_CONF_ERROR;

    name.data = ngx_palloc(cf->pool, name.len);
    if (!name.data)
        return NGX_CONF_ERROR;

    ngx_http_graphite_template_execute((char*)name.data, name.len, template, variables);

    return ngx_http_graphite_add_data(cf, splits, datas, &name, filter);
}

static char *
ngx_http_graphite_add_data(ngx_conf_t *cf, ngx_array_t *splits, ngx_array_t *datas, ngx_str_t *name, ngx_http_complex_value_t *filter) {

    ngx_uint_t s = SPLIT_EMPTY;

    ngx_uint_t i = 0;
    for (i = 0; i < splits->nelts; i++) {
        ngx_str_t *split = &((ngx_str_t*)splits->elts)[i];
        if ((split->len == name->len) && !ngx_strncmp(split->data, name->data, name->len)) {
            s = i;
            break;
        }
    }

    if (s == SPLIT_EMPTY) {

        s = splits->nelts;

        ngx_str_t *split = ngx_array_push(splits);
        if (!split) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }

        split->data = ngx_pstrdup(cf->pool, name);
        if (!split->data) {
            ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
            return NGX_CONF_ERROR;
        }

        split->len = name->len;
    }

    ngx_http_graphite_data_t *data = ngx_array_push(datas);
    if (!data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }

    data->split = s;
    data->filter = filter;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_parse_string(ngx_conf_t *cf, ngx_str_t *value, ngx_str_t *result) {

    result->data = ngx_pstrdup(cf->pool, value);
    if (!result->data) {
        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
        return NGX_CONF_ERROR;
    }

    result->len = value->len;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_parse_size(ngx_str_t *value, ngx_uint_t *result) {

    if (!result)
        return NGX_CONF_ERROR;

    size_t len = 0;
    while (len < value->len && value->data[len] >= '0' && value->data[len] <= '9')
        len++;

    *result = ngx_atoi(value->data, len);
    if (*result == (ngx_uint_t)-1)
        return NGX_CONF_ERROR;

    if (len + 1 == value->len) {
        if (value->data[len] == 'b')
            *result *= 1;
        else if (value->data[len] == 'k')
            *result *= 1024;
        else if (value->data[len] == 'm')
            *result *= 1024 * 1024;
        else
            return NGX_CONF_ERROR;

        len++;
    }

    if (len != value->len)
        return NGX_CONF_ERROR;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_parse_time(ngx_str_t *value, ngx_uint_t *result) {

    if (!result)
        return NGX_CONF_ERROR;

    size_t len = 0;
    while (len < value->len && value->data[len] >= '0' && value->data[len] <= '9')
        len++;

    *result = ngx_atoi(value->data, len);
    if (*result == (ngx_uint_t)-1)
        return NGX_CONF_ERROR;

    if (len + 1 == value->len) {
        if (value->data[len] == 's')
            *result *= 1;
        else if (value->data[len] == 'm')
            *result *= 60;
        else
            return NGX_CONF_ERROR;

        len++;
    }

    if (len != value->len)
        return NGX_CONF_ERROR;

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_template_compile(ngx_conf_t *cf, ngx_array_t *template, const ngx_http_graphite_template_arg_t *args, size_t nargs, const ngx_str_t *value) {

    ngx_uint_t i = 0;
    ngx_uint_t s = 0;

    typedef enum {
        TEMPLATE_STATE_ERROR = -1,
        TEMPLATE_STATE_NONE,
        TEMPLATE_STATE_VAR,
        TEMPLATE_STATE_BRACKET_VAR,
        TEMPLATE_STATE_VAR_START,
        TEMPLATE_STATE_BRACKET_VAR_START,
        TEMPLATE_STATE_BRACKET_VAR_END,
        TEMPLATE_STATE_NOP,
    } ngx_http_graphite_template_parser_state_t;

    typedef enum {
        TEMPLATE_LEXEM_ERROR = -1,
        TEMPLATE_LEXEM_VAR,
        TEMPLATE_LEXEM_BRACKET_OPEN,
        TEMPLATE_LEXEM_BRACKET_CLOSE,
        TEMPLATE_LEXEM_ALNUM,
        TEMPLATE_LEXEM_OTHER,
        TEMPLATE_LEXEM_END,
    } ngx_http_graphite_template_parser_lexem_t;

    ngx_http_graphite_template_parser_state_t state = TEMPLATE_STATE_NONE;
    ngx_http_graphite_template_parser_lexem_t lexem;

    ngx_http_graphite_template_parser_state_t parser[TEMPLATE_STATE_BRACKET_VAR_END + 1][TEMPLATE_LEXEM_END + 1] = {
        { 1,  6,  6,  6,  6,  0},
        { 0,  2, -1,  3, -1, -1},
        {-1, -1, -1,  4, -1, -1},
        { 0,  0,  0,  6,  0,  0},
        {-1, -1,  5,  6, -1, -1},
        { 0,  0,  0,  0,  0,  0},
    };

    for (i = 0; i <= value->len; i++) {
        if (i == value->len)
            lexem = TEMPLATE_LEXEM_END;
        else if (value->data[i] == '$')
            lexem = TEMPLATE_LEXEM_VAR;
        else if (value->data[i] == '(')
            lexem = TEMPLATE_LEXEM_BRACKET_OPEN;
        else if (value->data[i] == ')')
            lexem = TEMPLATE_LEXEM_BRACKET_CLOSE;
        else if (isalnum(value->data[i]))
            lexem = TEMPLATE_LEXEM_ALNUM;
        else
            lexem = TEMPLATE_LEXEM_OTHER;

        ngx_int_t new_state = parser[state][lexem];

        if (new_state == TEMPLATE_LEXEM_ERROR)
            return NGX_CONF_ERROR;

        if (new_state != TEMPLATE_STATE_NOP) {

            if (i != s && (state == TEMPLATE_STATE_NONE || state == TEMPLATE_STATE_VAR_START || state == TEMPLATE_STATE_BRACKET_VAR_START)) {

                ngx_http_graphite_template_t *arg = ngx_array_push(template);
                if (!arg) {
                    ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
                    return NGX_CONF_ERROR;
                }

                if (state == TEMPLATE_STATE_NONE) {
                    arg->data.data = ngx_palloc(cf->pool, i - s);
                    if (!arg->data.data) {
                        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite can't alloc memory");
                        return NGX_CONF_ERROR;
                    }
                    ngx_memcpy(arg->data.data, value->data + s, i - s);
                    arg->data.len = i - s;
                    arg->variable = 0;
                }
                else if (state == TEMPLATE_STATE_VAR_START || state == TEMPLATE_STATE_BRACKET_VAR_START) {
                    ngx_uint_t find = 0;
                    size_t a;
                    for (a = 0; a < nargs; a++) {
                        if ((args[a].name.len == i - s) && !ngx_strncmp(args[a].name.data, &value->data[s], i - s)) {
                            find = 1;
                            arg->variable = args[a].variable;
                            arg->data.data = NULL;
                            arg->data.len = 0;
                        }
                    }

                    if (!find) {
                        ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "graphite unknow template arg %*s", i - s, &value->data[s]);
                        return NGX_CONF_ERROR;
                    }
                }
            }

            s = i;
            state = new_state;
        }
    }

    return NGX_CONF_OK;
}

static char *
ngx_http_graphite_template_execute(char* buffer, size_t buffer_size, const ngx_array_t *template, const ngx_str_t *variables[]) {

    char *b = buffer;

    ngx_uint_t i;
    for (i = 0; i < template->nelts; i++) {
        ngx_http_graphite_template_t *arg = &((ngx_http_graphite_template_t*)template->elts)[i];
        const ngx_str_t *data = NULL;

        if (arg->data.len)
            data = &arg->data;
        else
            data = variables[arg->variable];

        b = (char*)ngx_snprintf((u_char*)b, buffer_size - (b - buffer), "%V", data);
    }

    return b;
}

static size_t
ngx_http_graphite_template_len(const ngx_array_t *template, const ngx_str_t *variables[]) {

    size_t len = 0;

    ngx_uint_t i;
    for (i = 0; i < template->nelts; i++) {
        ngx_http_graphite_template_t *arg = &((ngx_http_graphite_template_t*)template->elts)[i];

        if (arg->data.len)
            len += arg->data.len;
        else
            len += variables[arg->variable]->len;
    }

    return len;
}

static ngx_http_complex_value_t *
ngx_http_graphite_complex_compile(ngx_conf_t *cf, ngx_str_t *value) {

    ngx_http_compile_complex_value_t ccv;
    ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));

    ccv.cf = cf;
    ccv.value = value;
    ccv.complex_value = ngx_palloc(cf->pool, sizeof(ngx_http_complex_value_t));
    if (ccv.complex_value == NULL)
        return NULL;

    if (ngx_http_compile_complex_value(&ccv) != NGX_OK)
        return NULL;

    return ccv.complex_value;
}

static ngx_int_t
ngx_http_graphite_shared_init(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_http_graphite_main_conf_t *gmcf = shm_zone->data;

    if (data) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite shared memory data set");
        return NGX_ERROR;
    }

    size_t shared_required_size = 0;
    shared_required_size += sizeof(ngx_http_graphite_storage_t);
    shared_required_size += sizeof(ngx_http_graphite_acc_t) * (gmcf->max_interval + 1) * (gmcf->splits->nelts * gmcf->params->nelts + gmcf->custom_params->nelts);

    if (sizeof(ngx_slab_pool_t) + shared_required_size > shm_zone->shm.size) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite too small shared memory (minimum size is %uzb)", sizeof(ngx_slab_pool_t) + shared_required_size);
        return NGX_ERROR;
    }

    // 128 is the approximate size of the one udp record
    size_t buffer_required_size = (gmcf->splits->nelts * (gmcf->intervals->nelts * gmcf->params->nelts + 1) + gmcf->custom_params->nelts) * 128;
    if (buffer_required_size > gmcf->buffer_size) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite too small buffer size (minimum size is %uzb)", buffer_required_size);
        return NGX_ERROR;
    }

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t *)shm_zone->shm.addr;

    if (shm_zone->shm.exists) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite shared memory exists");
        return NGX_ERROR;
    }

    char *p = ngx_slab_alloc(shpool, shared_required_size);
    if (!p) {
        ngx_log_error(NGX_LOG_ERR, shm_zone->shm.log, 0, "graphite can't slab alloc in shared memory");
        return NGX_ERROR;
    }

    shpool->data = p;

    memset(p, 0, shared_required_size);

    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)p;
    p += sizeof(ngx_http_graphite_storage_t);

    storage->accs = (ngx_http_graphite_acc_t*)p;
    p += sizeof(ngx_http_graphite_acc_t) * (gmcf->max_interval + 1) * gmcf->splits->nelts * gmcf->params->nelts;

    storage->custom_accs = (ngx_http_graphite_acc_t*)p;
    p += sizeof(ngx_http_graphite_acc_t) * (gmcf->max_interval + 1) * gmcf->custom_params->nelts;

    time_t ts = ngx_time();
    storage->start_time = ts;
    storage->last_time = ts;
    storage->event_time = ts;

    return NGX_OK;
}

static void
ngx_http_graphite_add_param(ngx_http_request_t *r, ngx_http_graphite_storage_t *storage, time_t ts, ngx_uint_t split, ngx_uint_t param, double value) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_get_module_main_conf(r, ngx_http_graphite_module);

    ngx_uint_t m = ((ts - storage->start_time) % (gmcf->max_interval + 1)) * gmcf->splits->nelts * gmcf->params->nelts + split * gmcf->params->nelts + param;
    ngx_http_graphite_acc_t *acc = &storage->accs[m];

    acc->value += value;
    acc->count++;
}

static void
ngx_http_graphite_add_datas(ngx_http_request_t *r, ngx_http_graphite_storage_t *storage, time_t ts, ngx_array_t *datas, ngx_uint_t param, double value) {

    ngx_uint_t i;
    for (i = 0; i < datas->nelts; i++) {
        ngx_http_graphite_data_t *data = &((ngx_http_graphite_data_t*)datas->elts)[i];
        if (data->filter) {
            ngx_str_t result;
            if (ngx_http_complex_value(r, data->filter, &result) != NGX_OK)
                continue;

            if (result.len == 0 || (result.len == 1 && result.data[0] == '0'))
                continue;
        }

        ngx_http_graphite_add_param(r, storage, ts, data->split, param, value);
    }
}

ngx_int_t
ngx_http_graphite_handler(ngx_http_request_t *r) {

    ngx_http_graphite_main_conf_t *gmcf;
    ngx_http_graphite_srv_conf_t *gscf;
    ngx_http_graphite_loc_conf_t *glcf;

    gmcf = ngx_http_get_module_main_conf(r, ngx_http_graphite_module);
    gscf = ngx_http_get_module_srv_conf(r, ngx_http_graphite_module);
    glcf = ngx_http_get_module_loc_conf(r, ngx_http_graphite_module);

    if (!gmcf->enable)
        return NGX_OK;

    if (gmcf->datas->nelts == 0 && gscf->datas->nelts == 0 && glcf->datas->nelts == 0)
        return NGX_OK;

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)shpool->data;

    time_t ts = ngx_time();

    double *params = ngx_http_graphite_get_params(r);
    if (params == NULL) {

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "graphite can't get params");
        return NGX_OK;
    }

    ngx_shmtx_lock(&shpool->mutex);
    ngx_http_graphite_del_old_records(gmcf, ts);

    ngx_uint_t p;
    for (p = 0; p < gmcf->params->nelts; p++) {
        ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->params->elts)[p];

        if (param->get) {
            if (r == r->main) {
                ngx_http_graphite_add_datas(r, storage, ts, gmcf->datas, p, params[p]);
                ngx_http_graphite_add_datas(r, storage, ts, gscf->datas, p, params[p]);
            }
            ngx_http_graphite_add_datas(r, storage, ts, glcf->datas, p, params[p]);
        }
    }

    ngx_shmtx_unlock(&shpool->mutex);

    return NGX_OK;
}

static ngx_int_t
ngx_http_graphite_custom(ngx_http_request_t *r, ngx_str_t *name, double value) {

    ngx_http_graphite_main_conf_t *gmcf;

    gmcf = ngx_http_get_module_main_conf(r, ngx_http_graphite_module);

    if (!gmcf->enable)
        return NGX_OK;

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)shpool->data;

    time_t ts = ngx_time();

    ngx_shmtx_lock(&shpool->mutex);

    ngx_http_graphite_del_old_records(gmcf, ts);

    ngx_uint_t key = ngx_hash_key_lc(name->data, name->len);
    ngx_uint_t p = (ngx_uint_t)ngx_hash_find(&gmcf->custom_params_hash, key, name->data, name->len);

    if (p) {
        ngx_uint_t m = ((ts - storage->start_time) % (gmcf->max_interval + 1)) * gmcf->custom_params->nelts + (p - 1);
        ngx_http_graphite_acc_t *acc = &storage->custom_accs[m];

        acc->value += value;
        acc->count++;
    }

    ngx_shmtx_unlock(&shpool->mutex);

    return NGX_OK;
}

static void
ngx_http_graphite_timer_event_handler(ngx_event_t *ev) {

    time_t ts = ngx_time();

    ngx_http_graphite_main_conf_t *gmcf;
    gmcf = ev->data;

    char *buffer = gmcf->buffer;
    char *b = buffer;

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)shpool->data;

    ngx_shmtx_lock(&shpool->mutex);

    if ((ngx_uint_t)(ts - storage->event_time) * 1000 < gmcf->frequency) {
        ngx_shmtx_unlock(&shpool->mutex);
        if (!(ngx_quit || ngx_terminate || ngx_exiting))
            ngx_add_timer(ev, gmcf->frequency);
        return;
    }

    storage->event_time = ts;

    ngx_http_graphite_del_old_records(gmcf, ts);

    ngx_uint_t i, s, p;
    for (i = 0; i < gmcf->intervals->nelts; i++) {
        const ngx_http_graphite_interval_t *interval = &((ngx_http_graphite_interval_t*)gmcf->intervals->elts)[i];

        for (s = 0; s < gmcf->splits->nelts; s++) {
            const ngx_str_t *split = &(((ngx_str_t*)gmcf->splits->elts)[s]);

            for (p = 0; p < gmcf->params->nelts; p++) {
                const ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->params->elts)[p];

                ngx_http_graphite_acc_t a;
                a.value = 0;
                a.count = 0;

                unsigned l;
                for (l = 0; l < interval->value; l++) {
                    if ((time_t)(ts - l - 1) >= storage->start_time) {
                        ngx_uint_t m = ((ts - l - 1 - storage->start_time) % (gmcf->max_interval + 1)) * gmcf->splits->nelts * gmcf->params->nelts + s * gmcf->params->nelts + p;
                        ngx_http_graphite_acc_t *acc = &storage->accs[m];
                        a.value += acc->value;
                        a.count += acc->count;
                    }
                }

                double value = param->aggregate(interval, &a);
                if (!gmcf->template->nelts) {
                    if (gmcf->prefix.len)
                        b = (char*)ngx_snprintf((u_char*)b, gmcf->buffer_size - (b - buffer), "%V.", &gmcf->prefix);
                    b = (char*)ngx_snprintf((u_char*)b, gmcf->buffer_size - (b - buffer), "%V.%V.%V_%V %.3f %T\n", &gmcf->host, split, &param->name, &interval->name, value, ts);
                }
                else {
                    const ngx_str_t *variables[] = TEMPLATE_VARIABLES(&gmcf->prefix, &gmcf->host, split, &param->name, &interval->name);
                    b = ngx_http_graphite_template_execute(b, gmcf->buffer_size - (b - buffer), gmcf->template, variables);
                    b = (char*)ngx_snprintf((u_char*)b, gmcf->buffer_size - (b - buffer), " %.3f %T\n", value, ts);
                }
            }
        }
    }

    for (p = 0; p < gmcf->custom_params->nelts; p++) {
        ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->custom_params->elts)[p];

        ngx_http_graphite_acc_t a;
        a.value = 0;
        a.count = 0;

        unsigned l;
        for (l = 0; l < param->interval.value; l++) {
            if ((time_t)(ts - l - 1) >= storage->start_time) {
                ngx_uint_t m = ((ts - l - 1 - storage->start_time) % (gmcf->max_interval + 1)) * gmcf->custom_params->nelts + p;
                ngx_http_graphite_acc_t *acc = &storage->custom_accs[m];
                a.value += acc->value;
                a.count += acc->count;
            }
         }

         double value = param->aggregate(&param->interval, &a);
         if (gmcf->prefix.len)
             b = (char*)ngx_snprintf((u_char*)b, gmcf->buffer_size - (b - buffer), "%V.", &gmcf->prefix);
         b = (char*)ngx_snprintf((u_char*)b, gmcf->buffer_size - (b - buffer), "%V.%V %.3f %T\n", &gmcf->host, &param->name, value, ts);
    }

    ngx_shmtx_unlock(&shpool->mutex);

    if (b == buffer + gmcf->buffer_size) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "graphite buffer size is too small");
        if (!(ngx_quit || ngx_terminate || ngx_exiting))
            ngx_add_timer(ev, gmcf->frequency);
        return;
    }
    *b = '\0';

    if (b != buffer) {
        struct sockaddr_in sin;
        memset((char *) &sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_addr = gmcf->server;
        sin.sin_port = htons(gmcf->port);

        char *part = buffer;
        char *next = NULL;
        char *nl = NULL;

        int fd;
         if (ngx_strcmp(gmcf->protocol.data, "tcp") == 0) {
             fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
         } else {
             fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
         }

        if (fd < 0) {
            ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "graphite can't create socket");
            if (!(ngx_quit || ngx_terminate || ngx_exiting))
                ngx_add_timer(ev, gmcf->frequency);
            return;
        }

        if (ngx_strcmp(gmcf->protocol.data, "tcp") == 0) {

            int flags;
            fd_set myset;

            /* Set socket to non-blocking */
            if ((flags = fcntl(fd, F_GETFL, 0)) < 0)
            {
                ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "failed to get the current non-blocking flag");
                if (!(ngx_quit || ngx_terminate || ngx_exiting))
                    ngx_add_timer(ev, gmcf->frequency);
                return;
            }

            if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
            {
                ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "failed to set the socket non-blocking");
                if (!(ngx_quit || ngx_terminate || ngx_exiting))
                    ngx_add_timer(ev, gmcf->frequency);
                return;
            }
              int res = connect(fd, (struct sockaddr *)&sin, sizeof(sin));
              if (res < 0) {
                 if (errno == EINPROGRESS) {
                    FD_ZERO(&myset);
                    FD_SET(fd, &myset);
                    if (!FD_ISSET(fd, &myset)) {
                      ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "File descriptor is NOT set");
                      if (!(ngx_quit || ngx_terminate || ngx_exiting))
                          ngx_add_timer(ev, gmcf->frequency);
                      return;
                    }
                 }
                 else {
                    ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "Error connecting %d - %s", errno, strerror(errno));
                    if (!(ngx_quit || ngx_terminate || ngx_exiting))
                        ngx_add_timer(ev, gmcf->frequency);
                    return;
                 }
              }

              // Set to blocking mode again...
              if( (flags = fcntl(fd, F_GETFL, NULL)) < 0) {
                    ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "failed to get the current non-blocking flag");
                    if (!(ngx_quit || ngx_terminate || ngx_exiting))
                        ngx_add_timer(ev, gmcf->frequency);
                    return;
              }
              flags &= (~O_NONBLOCK);
              if( fcntl(fd, F_SETFL, flags) < 0) {
                ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "failed to set the socket blocking again");
                if (!(ngx_quit || ngx_terminate || ngx_exiting))
                    ngx_add_timer(ev, gmcf->frequency);
                return;
              }
        }


        while (*part) {
            next = part;
            nl = part;

            while ((next = strchr(next, '\n')) && ((size_t)(next - part) <= gmcf->package_size)) {
                nl = next;
                next++;
            }

            if (nl > part) {

                if (ngx_strcmp(gmcf->protocol.data, "udp") == 0) {
                    if (sendto(fd, part, nl - part + 1, 0, (struct sockaddr*)&sin, sizeof(sin)) == -1)
                        ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "graphite can't send udp packet");
                } else if (ngx_strcmp(gmcf->protocol.data, "tcp") == 0) {
                    if(send(fd, part, nl - part + 1, 0) < 0) {
                        if(errno == EWOULDBLOCK) {
                            ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "buffer is full");
                        } else {
                              ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "graphite can't send tcp packet");
                        }
                    }
                }
            }
            else {
                ngx_log_error(NGX_LOG_ALERT, ev->log, 0, "graphite package size too small, need send %z", (size_t)(next - part));
            }

            part = nl + 1;
        }

        close(fd);
    }

    if (!(ngx_quit || ngx_terminate || ngx_exiting))
        ngx_add_timer(ev, gmcf->frequency);
}

void
ngx_http_graphite_del_old_records(ngx_http_graphite_main_conf_t *gmcf, time_t ts) {

    ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)gmcf->shared->shm.addr;
    ngx_http_graphite_storage_t *storage = (ngx_http_graphite_storage_t*)shpool->data;

    ngx_uint_t s, p;

    while ((ngx_uint_t)storage->last_time + gmcf->max_interval < (ngx_uint_t)ts) {

        for (s = 0; s < gmcf->splits->nelts; s++) {

            for (p = 0; p < gmcf->params->nelts; p++) {
                ngx_uint_t a = ((storage->last_time - storage->start_time) % (gmcf->max_interval + 1)) * gmcf->splits->nelts * gmcf->params->nelts + s * gmcf->params->nelts + p;
                ngx_http_graphite_acc_t *acc = &storage->accs[a];

                acc->value = 0;
                acc->count = 0;
            }
        }

        for (p = 0; p < gmcf->custom_params->nelts; p++) {
            ngx_uint_t a = ((storage->last_time - storage->start_time) % (gmcf->max_interval + 1)) * gmcf->custom_params->nelts + p;
            ngx_http_graphite_acc_t *acc = &storage->custom_accs[a];

            acc->value = 0;
            acc->count = 0;
        }

        storage->last_time++;
    }
}

static double *
ngx_http_graphite_get_params(ngx_http_request_t *r) {

    ngx_http_graphite_main_conf_t *gmcf;
    gmcf = ngx_http_get_module_main_conf(r, ngx_http_graphite_module);

    double *params = ngx_palloc(r->pool, sizeof(double) * gmcf->params->nelts);
    if (!params)
        return NULL;

    ngx_uint_t p;
    for (p = 0; p < gmcf->params->nelts; p++) {

        ngx_http_graphite_param_t *param = &((ngx_http_graphite_param_t*)gmcf->params->elts)[p];
        if (param->get)
            params[p] = param->get(r);
    }

    return params;
}

static double
ngx_http_graphite_param_request_time(ngx_http_request_t *r) {

    ngx_time_t      *tp;
    ngx_msec_int_t   ms;

    tp = ngx_timeofday();
    ms = (ngx_msec_int_t) ((tp->sec - r->start_sec) * 1000 + (tp->msec - r->start_msec));

    return (double)ms;
}

static double
ngx_http_graphite_param_bytes_sent(ngx_http_request_t *r) {

    return (double)r->connection->sent;
}

static double
ngx_http_graphite_param_body_bytes_sent(ngx_http_request_t *r) {

    off_t  length;

    length = r->connection->sent - r->header_size;
    length = ngx_max(length, 0);

    return (double)length;
}

static double
ngx_http_graphite_param_request_length(ngx_http_request_t *r) {

    return (double)r->request_length;
}

static double
ngx_http_graphite_param_ssl_handshake_time(ngx_http_request_t *r) {

    ngx_msec_int_t ms;

    ms = 0;

#if (defined(NGX_GRAPHITE_PATCH) && (NGX_SSL))
    if (r->connection->requests == 1) {
        ngx_ssl_connection_t *ssl = r->connection->ssl;
        if (ssl)
            ms = (ngx_msec_int_t)((ssl->handshake_end_sec - ssl->handshake_start_sec) * 1000 + (ssl->handshake_end_msec - ssl->handshake_start_msec));
    }
#endif

    return (double)ms;
}

static double
ngx_http_graphite_param_ssl_cache_usage(ngx_http_request_t *r) {

    double usage;

    usage = 0;

#if (defined(NGX_GRAPHITE_PATCH) && (NGX_SSL))
    ngx_ssl_connection_t *ssl = r->connection->ssl;
    if (ssl) {

        SSL_CTX *ssl_ctx = SSL_get_SSL_CTX(ssl->connection);
        ngx_shm_zone_t *shm_zone = SSL_CTX_get_ex_data(ssl_ctx, ngx_ssl_session_cache_index);
        if (shm_zone) {
            ngx_slab_pool_t *shpool = (ngx_slab_pool_t*)shm_zone->shm.addr;

            ngx_shmtx_lock(&shpool->mutex);

            ngx_uint_t all_pages = (shpool->end - shpool->start) / ngx_pagesize;
            ngx_uint_t free_pages = 0;

            ngx_slab_page_t *page;
            for (page = shpool->free.next; page != &shpool->free; page = page->next)
                free_pages += page->slab;

            ngx_shmtx_unlock(&shpool->mutex);

            if (all_pages > 0)
                usage = (100 * (all_pages - free_pages)) / all_pages;
        }
    }
#endif

    return usage;
}

static double
ngx_http_graphite_param_content_time(ngx_http_request_t *r) {

    ngx_msec_int_t ms;

    ms = 0;

#if (defined(NGX_GRAPHITE_PATCH))
    ms = (ngx_msec_int_t)((r->content_end_sec - r->content_start_sec) * 1000 + (r->content_end_msec - r->content_start_msec));
#endif

    ms = ngx_max(ms, 0);

    return (double)ms;
}

static double
ngx_http_graphite_param_gzip_time(ngx_http_request_t *r) {

    ngx_msec_int_t ms;

    ms = 0;

#if (defined(NGX_GRAPHITE_PATCH) && (NGX_HTTP_GZIP))
    ms = (ngx_msec_int_t)((r->gzip_end_sec - r->gzip_start_sec) * 1000 + (r->gzip_end_msec - r->gzip_start_msec));
#endif
    ms = ngx_max(ms, 0);

    return (double)ms;
}

static double
ngx_http_graphite_param_upstream_time(ngx_http_request_t *r) {

    ngx_uint_t i;
    ngx_msec_int_t ms;
    ngx_http_upstream_state_t *state;

    ms = 0;

    if (r->upstream_states == NULL)
        return 0;

    state = r->upstream_states->elts;

    for (i = 0 ; i < r->upstream_states->nelts; i++) {
        if (state[i].status)
#if nginx_version >= 1009001
            ms += (ngx_msec_int_t)(state[i].response_time);
#else
            ms += (ngx_msec_int_t)(state[i].response_sec * 1000 + state[i].response_msec);
#endif
    }
    ms = ngx_max(ms, 0);

    return (double)ms;
}

#if nginx_version >= 1009001
static double
ngx_http_graphite_param_upstream_connect_time(ngx_http_request_t *r) {

    ngx_uint_t i;
    ngx_msec_int_t ms;
    ngx_http_upstream_state_t *state;

    ms = 0;

    if (r->upstream_states == NULL)
        return 0;

    state = r->upstream_states->elts;

    for (i = 0 ; i < r->upstream_states->nelts; i++) {
        if (state[i].status)
            ms += (ngx_msec_int_t)(state[i].connect_time);
    }
    ms = ngx_max(ms, 0);

    return (double)ms;
}

static double
ngx_http_graphite_param_upstream_header_time(ngx_http_request_t *r) {

    ngx_uint_t i;
    ngx_msec_int_t ms;
    ngx_http_upstream_state_t *state;

    ms = 0;

    if (r->upstream_states == NULL)
        return 0;

    state = r->upstream_states->elts;

    for (i = 0 ; i < r->upstream_states->nelts; i++) {
        if (state[i].status)
            ms += (ngx_msec_int_t)(state[i].header_time);
    }
    ms = ngx_max(ms, 0);

    return (double)ms;
}
#endif

static double
ngx_http_graphite_param_rps(ngx_http_request_t *r) {

    return 1;
}

static double
ngx_http_graphite_param_keepalive_rps(ngx_http_request_t *r) {

    if (r->connection->requests == 1)
        return 0;
    else
        return 1;
}

static double
ngx_http_graphite_param_response_2xx_rps(ngx_http_request_t *r) {

    if (r->headers_out.status / 100 == 2)
        return 1;
    else
        return 0;
}

static double
ngx_http_graphite_param_response_3xx_rps(ngx_http_request_t *r) {

    if (r->headers_out.status / 100 == 3)
        return 1;
    else
        return 0;
}

static double
ngx_http_graphite_param_response_4xx_rps(ngx_http_request_t *r) {

    if (r->headers_out.status / 100 == 4)
        return 1;
    else
        return 0;
}

static double
ngx_http_graphite_param_response_5xx_rps(ngx_http_request_t *r) {

    if (r->headers_out.status / 100 == 5)
        return 1;
    else
        return 0;
}

static double
ngx_http_graphite_aggregate_avg(const ngx_http_graphite_interval_t *interval, const ngx_http_graphite_acc_t *acc) {

    return (acc->count != 0) ? acc->value / acc->count : 0;
}

static double
ngx_http_graphite_aggregate_persec(const ngx_http_graphite_interval_t *interval, const ngx_http_graphite_acc_t *acc) {

    return acc->value / interval->value;
}

static double
ngx_http_graphite_aggregate_sum(const ngx_http_graphite_interval_t *interval, const ngx_http_graphite_acc_t *acc) {

    return acc->value;
}

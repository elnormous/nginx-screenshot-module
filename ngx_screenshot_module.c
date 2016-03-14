#include <magick/MagickCore.h>
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

static char *ngx_screenshot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static void * ngx_screenshot_create_loc_conf(ngx_conf_t *cf);
static char * ngx_screenshot_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
    
static char *ngx_screenshot_sizes_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static ngx_int_t ngx_screenshot_handler(ngx_http_request_t *r);

#define NGX_RTMP_CONTROL_ALL        0xff
#define NGX_RTMP_CONTROL_SMALL      0x01
#define NGX_RTMP_CONTROL_MEDIUM     0x02
#define NGX_RTMP_CONTROL_LARGE      0x04

typedef struct {
    ngx_array_t             sizes;
} ngx_screenshot_sizes_conf_t;

typedef struct {
    ngx_str_t               name;
    ngx_int_t               width;
    ngx_int_t               height;
} ngx_screenshot_size_t;

typedef struct {
    ngx_uint_t                      control;
} ngx_screenshot_conf_t;

typedef struct {
    ngx_array_t             sizes;
} ngx_screenshot_sizes_conf_ctx_t;

static ngx_conf_bitmask_t           ngx_screenshot_masks[] = {
    { ngx_string("all"),            NGX_RTMP_CONTROL_ALL         },
    { ngx_string("small"),          NGX_RTMP_CONTROL_SMALL       },
    { ngx_string("medium"),         NGX_RTMP_CONTROL_MEDIUM      },
    { ngx_string("large"),          NGX_RTMP_CONTROL_LARGE       },
    { ngx_null_string,              0                            }
};

static ngx_command_t ngx_screenshot_commands[] = {
    
    { ngx_string("screenshot"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_screenshot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_screenshot_conf_t, control),
      ngx_screenshot_masks },
      
    { ngx_string("screenshot_sizes"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_screenshot_sizes_block,
      0,
      0,
      NULL },
    
    ngx_null_command
};

static ngx_http_module_t ngx_screenshot_module_ctx = {
    NULL,                                 /* preconfiguration */
    NULL,                                 /* postconfiguration */
    NULL,                                 /* create main configuration */
    NULL,                                 /* init main configuration */
    NULL,                                 /* create server configuration */
    NULL,                                 /* merge server configuration */
    ngx_screenshot_create_loc_conf,           /* create location configuration */
    ngx_screenshot_merge_loc_conf             /* merge location configuration */
};

ngx_module_t ngx_screenshot_module = {
    NGX_MODULE_V1,
    &ngx_screenshot_module_ctx,      /* module context */
    ngx_screenshot_commands,         /* module directives */
    NGX_HTTP_MODULE,                 /* module type */
    NULL,                            /* init master */
    NULL,                            /* init module */
    NULL,                            /* init process */
    NULL,                            /* init thread */
    NULL,                            /* exit thread */
    NULL,                            /* exit process */
    NULL,                            /* exit master */
    NGX_MODULE_V1_PADDING
};

char** screenshot_sizes = 0;

static ngx_int_t
ngx_screenshot_handler(ngx_http_request_t *r)
{
    size_t                              len;
    u_char                             *p;
    ngx_buf_t                          *b;
    ngx_chain_t                         cl;
    u_char                              path[1024];
    ExceptionInfo                      *exception;
    Image                              *image = 0, *resized = 0;
    ImageInfo                          *image_info = 0;
    unsigned char                      *image_data;
    ngx_str_t                           size;
    ngx_int_t                           width = -1, height = -1;
    ngx_screenshot_sizes_conf_t        *screenshot_sizes_conf = ngx_event_get_conf(ngx_cycle->conf_ctx, ngx_screenshot_module);

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "TEST %d", screenshot_sizes_conf->sizes.nelts);
    
    //TODO: generate unique name
    system("avconv -analyzeduration 1000 -i \"rtmp://127.0.0.1:1935\" -vframes 1 -q:v 2 -f image2 /tmp/output.png -loglevel quiet");

    MagickCoreGenesis("", MagickTrue);
    exception = AcquireExceptionInfo();
    
    ngx_memzero(path, sizeof(path));
    
    strcpy((char *)path, "/tmp/output.png");
    
    image_info = CloneImageInfo((ImageInfo *)NULL);
    strcpy(image_info->filename, (const char*)path);
    
    image = ReadImage(image_info, exception);
    
    if (image == 0) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to open screenshot file");
        goto error;
    }
    
    if (ngx_http_arg(r, (u_char *) "size", sizeof("size") - 1, &size) == NGX_OK) {
        
        if (size.len > 0) {
            if (size.data[0] == 'l' && size.data[1] == 'o' && size.data[2] == 'w') {
                width = 320;
                height = 180;
            }
            else if (size.data[0] == 'm' && size.data[1] == 'e' && size.data[2] == 'd') {
                width = 640;
                height = 360;
            }
            else if (size.data[0] == 'h' && size.data[1] == 'i') {
                width = 960;
                height = 540;
            }
            else if (size.data[0] == 'h' && size.data[1] == 'd') {
                width = 1280;
                height = 720;
            }
        }
    }
    
    if (width < 1 || width > (ngx_int_t)image->columns) {
        width = image->columns;
    }
    
    if (height < 1 || height > (ngx_int_t)image->rows) {
        height = image->rows;
    }
    
    if (width == (ngx_int_t)image->columns && height == (ngx_int_t)image->rows) {
        resized = image;
    }
    else {
        resized = ResizeImage(image, width, height, SincFilter, 1.0, exception);
    }
    
    if (resized == 0) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to resize screenshot file");
        goto error;
    }
    
    image_data = ImageToBlob(image_info, resized, &len, exception);
    
    p = ngx_palloc(r->connection->pool, len);
    if (p == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to allocate memory for response");
        goto error;
    }
    
    ngx_memcpy(p, image_data, len);
    
    if (resized != image) {
        DestroyImage(resized);
    }
    
    DestroyImage(image);
    MagicComponentTerminus();
    
    ngx_str_set(&r->headers_out.content_type, "image/png");
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = len;
    
    b = ngx_calloc_buf(r->pool);
    if (b == NULL) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0, "Failed to allocate memory for response");
        goto error;
    }
    
    b->start = b->pos = p;
    b->end = b->last = p + len;
    b->temporary = 1;
    b->last_buf = 1;
    
    ngx_memzero(&cl, sizeof(cl));
    cl.buf = b;
    
    ngx_http_send_header(r);
    
    return ngx_http_output_filter(r, &cl);
    
error:
    if (resized && resized != image) DestroyImage(resized);
    if (image) DestroyImage(image);
    
    MagicComponentTerminus();
    
    return NGX_HTTP_INTERNAL_SERVER_ERROR;
}

static void *
ngx_screenshot_create_loc_conf(ngx_conf_t *cf)
{
    ngx_screenshot_conf_t  *conf;
    
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_screenshot_conf_t));
    if (conf == NULL) {
        return NGX_CONF_ERROR;
    }
    
    conf->control = 0;
    
    return conf;
}


static char *
ngx_screenshot_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_screenshot_conf_t  *prev = parent;
    ngx_screenshot_conf_t  *conf = child;
    
    ngx_conf_merge_bitmask_value(conf->control, prev->control, 0);
    
    return NGX_CONF_OK;
}

static char *
ngx_screenshot(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;
    
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_screenshot_handler;
    
    return ngx_conf_set_bitmask_slot(cf, cmd, conf);
}

static char *
ngx_http_split_sizes(ngx_conf_t *cf, ngx_command_t *dummy, void *conf)
{
    ngx_str_t                           *value;
    ngx_screenshot_sizes_conf_ctx_t     *ctx;
    ngx_screenshot_size_t               *size;
    
    ctx = cf->ctx;
    
    if (cf->args->nelts != 3) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid number of arguments in \"screenshot_sizes\" directive");
        
        return NGX_CONF_ERROR;
    }
    
    value = cf->args->elts;
    
    size = ngx_array_push(&ctx->sizes);
    if (size == NULL) {
        return NGX_CONF_ERROR;
    }
    
    size->name = value[0];
    size->width = ngx_atoi(value[1].data, value[1].len);
    size->height = ngx_atoi(value[2].data, value[2].len);
    
    printf("TEST: %s, %ld, %ld\n", size->name.data, size->width, size->height);
    
    return NGX_CONF_OK;
}

static char *
ngx_screenshot_sizes_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    //ngx_str_t                           *value;
    //char                                *rv;
    /*ngx_uint_t                               i;
     ngx_conf_t                               pcf;*/
    ngx_screenshot_sizes_conf_ctx_t     *ctx;
    ngx_conf_t                           save;
    //ngx_screenshot_sizes_conf_t             *cmcf;
    
    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_screenshot_sizes_conf_ctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }
    
    //value = cf->args->elts; // size name
    
    if (ngx_array_init(&ctx->sizes, cf->pool, 2,
                       sizeof(ngx_screenshot_size_t))
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }
    
    save = *cf;
    cf->ctx = ctx;
    cf->handler = ngx_http_split_sizes;
    cf->handler_conf = conf;
    //cf->cmd_type = NGX_CONF_BLOCK;

    //rv = ngx_conf_parse(cf, NULL);
    
    *cf = save;
    
    return NGX_CONF_OK;
}

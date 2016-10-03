
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

/* memcachedģ��loc������������ṹ�� */
typedef struct {
    ngx_http_upstream_conf_t   upstream;  // memcachedģ����ʹ�õ�upstream���ƶ������ýṹ��
    ngx_int_t                  index;  // ���memcached_key�������±�
    ngx_uint_t                 gzip_flag;  //memcached_gzip_flag����������
} ngx_http_memcached_loc_conf_t;


typedef struct {
    /*
     * ��ʾʣ��δ���յı�־����memcached��������Ч��Ӧ�����Ѿ������ı�־�еĳ��ȣ�
     * ��־������ΪCRLF "END" CRLF�����������Ч��Ӧ����ĺ���
     */
    size_t                     rest;
    ngx_http_request_t        *request;  // ʹ��memcachedģ����������
    /*
     * ָ�����Ŵ�r->variables���������ı���memcached_key��ֵ���ڴ棬�ⲿ���ڴ���ʵ�����ڴ����
     * ���͸�����memcached��������������ڴ��У�������ngx_http_memcached_create_request()
     */
    ngx_str_t                  key;
} ngx_http_memcached_ctx_t;


static ngx_int_t ngx_http_memcached_create_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_reinit_request(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_process_header(ngx_http_request_t *r);
static ngx_int_t ngx_http_memcached_filter_init(void *data);
static ngx_int_t ngx_http_memcached_filter(void *data, ssize_t bytes);
static void ngx_http_memcached_abort_request(ngx_http_request_t *r);
static void ngx_http_memcached_finalize_request(ngx_http_request_t *r,
    ngx_int_t rc);

static void *ngx_http_memcached_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_memcached_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);

static char *ngx_http_memcached_pass(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_conf_bitmask_t  ngx_http_memcached_next_upstream_masks[] = {
    { ngx_string("error"), NGX_HTTP_UPSTREAM_FT_ERROR },
    { ngx_string("timeout"), NGX_HTTP_UPSTREAM_FT_TIMEOUT },
    { ngx_string("invalid_response"), NGX_HTTP_UPSTREAM_FT_INVALID_HEADER },
    { ngx_string("not_found"), NGX_HTTP_UPSTREAM_FT_HTTP_404 },
    { ngx_string("off"), NGX_HTTP_UPSTREAM_FT_OFF },
    { ngx_null_string, 0 }
};

/* memcahcedģ��֧�ֵ�����ָ�� */
static ngx_command_t  ngx_http_memcached_commands[] = {

    { ngx_string("memcached_pass"),
      NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
      ngx_http_memcached_pass,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("memcached_bind"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_http_upstream_bind_set_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.local),
      NULL },

    { ngx_string("memcached_connect_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.connect_timeout),
      NULL },

    { ngx_string("memcached_send_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.send_timeout),
      NULL },

    { ngx_string("memcached_buffer_size"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_size_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.buffer_size),
      NULL },

    { ngx_string("memcached_read_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.read_timeout),
      NULL },

    { ngx_string("memcached_next_upstream"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_conf_set_bitmask_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.next_upstream),
      &ngx_http_memcached_next_upstream_masks },

    { ngx_string("memcached_next_upstream_tries"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.next_upstream_tries),
      NULL },

    { ngx_string("memcached_next_upstream_timeout"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, upstream.next_upstream_timeout),
      NULL },

    { ngx_string("memcached_gzip_flag"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_memcached_loc_conf_t, gzip_flag),
      NULL },

      ngx_null_command
};

/* memcachedģ��ʵ�ֵ�http����ģ��������� */
static ngx_http_module_t  ngx_http_memcached_module_ctx = {
    NULL,                                  /* preconfiguration */
    NULL,                                  /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_memcached_create_loc_conf,    /* create location configuration */
    ngx_http_memcached_merge_loc_conf      /* merge location configuration */
};

/* memcachedģ��ʵ�ֵ�Nginx��ģ���ͨ�ýӿ� */
ngx_module_t  ngx_http_memcached_module = {
    NGX_MODULE_V1,
    &ngx_http_memcached_module_ctx,        /* module context */
    ngx_http_memcached_commands,           /* module directives */
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

/* �����ļ���ʹ�õ��ı���memcached_key */
static ngx_str_t  ngx_http_memcached_key = ngx_string("memcached_key");


#define NGX_HTTP_MEMCACHED_END   (sizeof(ngx_http_memcached_end) - 1)
static u_char  ngx_http_memcached_end[] = CRLF "END" CRLF;

/*
 * ngx_http_memcached_moduleģ��Ĺ�����ں��������ʹ���˸�ģ�飬��ú������ջ����ø�
 * r->content_hanlder�ص���������NGX_HTTP_CONTENT_PHASE�׶λᱻ���á�
 */
static ngx_int_t
ngx_http_memcached_handler(ngx_http_request_t *r)
{
    ngx_int_t                       rc;
    ngx_http_upstream_t            *u;
    ngx_http_memcached_ctx_t       *ctx;
    ngx_http_memcached_loc_conf_t  *mlcf;

    /* ����ͻ���������GET����HEAD����ֱ�ӷ���NGX_HTTP_NOT_ALLOWED���ͻ��� */
    if (!(r->method & (NGX_HTTP_GET|NGX_HTTP_HEAD))) {
        return NGX_HTTP_NOT_ALLOWED;
    }

    /*
     * ��Ϊ��GET����HEAD���󣬲���memcachedģ��Ŀǰֻ֧�ִ�memcached��������ȡ���ݣ�
     * ���԰����ǲ���Ҫ�ģ��������ʱ�����Ҫ��ȡ����Ȼ������
     */
    rc = ngx_http_discard_request_body(r);

    if (rc != NGX_OK) {
        return rc;
    }

    /* �����������ݵ����� */
    if (ngx_http_set_content_type(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /*
     * ��Ϊ��Ҫ�ͺ�˵�memcached����������ͨ�ţ�������Ҫ�ȴ���upstream����upstream������
     * �������ʺ�˷������Ļ���������ű�Ҫ����Ϣ��������upstream����֮�󣬻���ص��������
     * ngx_http_request_t�е�upstream��Ա�С�
     */
    if (ngx_http_upstream_create(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u = r->upstream;

    /* ����upstream�����е�schema�ֶΣ�Ŀǰ���ֶ�ֻ���ڴ�ӡ��־ʹ�� */
    ngx_str_set(&u->schema, "memcached://");
    u->output.tag = (ngx_buf_tag_t) &ngx_http_memcached_module;

    mlcf = ngx_http_get_module_loc_conf(r, ngx_http_memcached_module);

    /* ��ngx_http_memcached_moduleģ���������Ϣ�л�ȡupstream���Ƶ�������Ϣ�������õ�upstream�����conf�� */
    u->conf = &mlcf->upstream;

    /* ����upstream���ƻ�ʹ�õ��ļ�����Ҫ�Ļص����� */
    u->create_request = ngx_http_memcached_create_request;
    u->reinit_request = ngx_http_memcached_reinit_request;
    u->process_header = ngx_http_memcached_process_header;
    u->abort_request = ngx_http_memcached_abort_request;
    u->finalize_request = ngx_http_memcached_finalize_request;

    /*
     * ����ngx_http_memcached_moduleģ��������Ľṹ�壬���Ը���ngx_http_memcached_moduleģ��
     * �������η�������Ӧ�Ĵ���
     */
    ctx = ngx_palloc(r->pool, sizeof(ngx_http_memcached_ctx_t));
    if (ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* ����ctx->requestΪ����˴δ�������� */
    ctx->request = r;

    ngx_http_set_ctx(r, ctx, ngx_http_memcached_module);

    /* ���ô��������صļ����ص������Լ����ݸ���Щ�ص��������û��Զ������� */
    u->input_filter_init = ngx_http_memcached_filter_init;
    u->input_filter = ngx_http_memcached_filter;
    u->input_filter_ctx = ctx;

    /*
     * ����ԭʼ������˵��ʹ��upstream�������˷���������������һ���������첽������������Ҫ
     * ��ԭʼ�����r->main->count������
     */
    r->main->count++;

    /* ��Ϊǰ���Ѿ���������upstream���󣬲���������һЩ��Ҫ�ĳ�ʼ�����������Կ�������upstream������ */
    ngx_http_upstream_init(r);

    return NGX_DONE;
}

/* ���췢�͸�����memcached���������������ɵ��������ݻ�����r->upstream->request_bufs�� */
static ngx_int_t
ngx_http_memcached_create_request(ngx_http_request_t *r)
{
    size_t                          len;
    uintptr_t                       escape;
    ngx_buf_t                      *b;
    ngx_chain_t                    *cl;
    ngx_http_memcached_ctx_t       *ctx;
    ngx_http_variable_value_t      *vv;
    ngx_http_memcached_loc_conf_t  *mlcf;

    /* ��ȡngx_http_memcached_moduleģ���loc������ṹ�� */
    mlcf = ngx_http_get_module_loc_conf(r, ngx_http_memcached_module);

    /* ��ȡ�����ļ�����set�������õ�memcached_key������ֵ */
    vv = ngx_http_get_indexed_variable(r, mlcf->index);

    if (vv == NULL || vv->not_found || vv->len == 0) {
        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                      "the \"$memcached_key\" variable is not set");
        return NGX_ERROR;
    }

    /* ��ȡ��Ҫ����ת����ֽڳ��� */
    escape = 2 * ngx_escape_uri(NULL, vv->data, vv->len, NGX_ESCAPE_MEMCACHED);

    /* ���������������г��� */
    len = sizeof("get ") - 1 + vv->len + escape + sizeof(CRLF) - 1;

    /* ��������������Ҫ���ڴ� */
    b = ngx_create_temp_buf(r->pool, len);
    if (b == NULL) {
        return NGX_ERROR;
    }

    /* ����һ��������������������������ڴ棬��Ҫ��Ϊ�˷�����ص�r->upstream->request_bufs������������ */
    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = b;
    cl->next = NULL;

    /* �������Ŵ�������ڴ�Ļ�����������ص�r->upstream->request_bufs�� */
    r->upstream->request_bufs = cl;

    /* ���濪ʼ���ڴ�������������ݣ�Ŀǰ�ٷ���memcachedģ��ֻ֧��get�������memcached��������ȡ���� */

    /* 1. ���'get'���� */
    *b->last++ = 'g'; *b->last++ = 'e'; *b->last++ = 't'; *b->last++ = ' ';

    /*
     * 2.������ڴ�memcached�������л�ȡ���ݵ�memcached_keyֵ��keyֵ�������ļ��е�set����ָ�����ڽ��������ļ���ʱ��
     * �Ѿ��������������������r->variables���ˡ�
     * ���˽�memcached_keyֵ��䵽�����У����Ὣkey��ֵ(ʵ��Ӧ��ֻ��ָ��keyֵ��ָ��)�����ngx_http_memcached_module
     * ģ�������ĵ�key�����С�
     */
    ctx = ngx_http_get_module_ctx(r, ngx_http_memcached_module);

    /*
     * ctx->keyҲָ����b->last�������Ὣmemcached_key������ֵ������b->lastָ����ڴ���.
     */
    ctx->key.data = b->last;

    if (escape == 0) {
        b->last = ngx_copy(b->last, vv->data, vv->len);

    } else {
        b->last = (u_char *) ngx_escape_uri(b->last, vv->data, vv->len,
                                            NGX_ESCAPE_MEMCACHED);
    }

    ctx->key.len = b->last - ctx->key.data;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http memcached request: \"%V\"", &ctx->key);

    /* 2. �����memcached_key��ֵ�����CR��LF */
    *b->last++ = CR; *b->last++ = LF;

    return NGX_OK;
}


static ngx_int_t
ngx_http_memcached_reinit_request(ngx_http_request_t *r)
{
    return NGX_OK;
}

/* ����������memcached���������յ�����Ӧͷ */
static ngx_int_t
ngx_http_memcached_process_header(ngx_http_request_t *r)
{
    u_char                         *p, *start;
    ngx_str_t                       line;
    ngx_uint_t                      flags;
    ngx_table_elt_t                *h;
    ngx_http_upstream_t            *u;
    ngx_http_memcached_ctx_t       *ctx;
    ngx_http_memcached_loc_conf_t  *mlcf;

    u = r->upstream;

    /* �������ʵ�ֿ��Կ�����memcachedЭ�����Ӧͷֻ��һ�� */

    /*
     * ����������memcached���������յ�����Ӧ���ݣ�һ�����ֳ�����LF�ַ����ͱ�ʾ��Ӧͷ�ҵ��ˣ�
     * �������Ϳ�ʼ������һ����Ӧͷ����
     */
    for (p = u->buffer.pos; p < u->buffer.last; p++) {
        if (*p == LF) {
            goto found;
        }
    }

    /* ����ִ�е����������û�н��յ���������Ӧͷ����Ҫ���������ν��ո������Ӧ���� */
    return NGX_AGAIN;

found:

    /* ��lineָ����u->buffer�е���Ӧͷ������ */
    line.data = u->buffer.pos;
    line.len = p - u->buffer.pos;

    /* ��Ӧͷ����Ӧ����֮���������CRLF��β�ģ��������LFǰ��һ���ַ�����CR���������Ӧ�����ǷǷ��ġ� */
    if (line.len == 0 || *(p - 1) != CR) {
        goto no_valid;
    }

    *p = '\0';
    line.len--;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "memcached: \"%V\"", &line);

    /* pָ������Ӧͷ���Ŀ�ʼ */
    p = u->buffer.pos;

    /* ��ȡngx_http_memcached_moduleģ��������ĺ�loc����������ṹ�� */
    ctx = ngx_http_get_module_ctx(r, ngx_http_memcached_module);
    mlcf = ngx_http_get_module_loc_conf(r, ngx_http_memcached_module);

    /* �ж���Ӧͷ���Ƿ���"VALUE "��ʼ */
    if (ngx_strncmp(p, "VALUE ", sizeof("VALUE ") - 1) == 0) {

        /* ����pָ����ڴ棬ƫ��֮��ָ����������memcached��������key��ֵ��ʼ�� */
        p += sizeof("VALUE ") - 1;

        /*
         * �Ƚ���Ӧͷ����keyֵ����������memcached������ʱ���ݸ�memcached��������keyֵ�Ƿ���ȣ�
         * ������߲���ȣ�˵��memcached��������ˣ����ʱ����Ҫ����NGX_HTTP_UPSTREAM_INVALID_HEADER��
         * ��ʾmemcached�����������˴������Ӧͷ
         */
        if (ngx_strncmp(p, ctx->key.data, ctx->key.len) != 0) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "memcached sent invalid key in response \"%V\" "
                          "for key \"%V\"",
                          &line, &ctx->key);

            return NGX_HTTP_UPSTREAM_INVALID_HEADER;
        }

        /* ����pָ����ڴ棬ƫ��֮��ָ�򷵻ص���Ӧͷ���е�keyֵ����һ����ַ */
        p += ctx->key.len;

        /* ��Ӧͷ���е�keyֵ֮������Ǹ��ո񣬷���Ҳ�ǷǷ���ͷ�� */
        if (*p++ != ' ') {
            goto no_valid;
        }

        /* flags */

        start = p;

        /*
         * ����ʣ����Ӧͷ���е�����ֱ�������ո�' '����������ļ���������memcached_gzip_flagָ�
         * ��ô����Ҫ�ж�memcached���������ص���Ӧ���������Ƿ�Ҳ�Ǿ�����gzipѹ���ġ����û������
         * memcached_gzip_flag����ô�ʹ���Ӧͷ��������Ӧ����ĳ���
         */
        while (*p) {
            if (*p++ == ' ') {
                if (mlcf->gzip_flag) {
                    goto flags;
                } else {
                    goto length;
                }
            }
        }

        goto no_valid;

    flags:

        /* ��ȡ��Ӧͷ���з��ص�flagֵ */
        flags = ngx_atoi(start, p - start - 1);

        if (flags == (ngx_uint_t) NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "memcached sent invalid flags in response \"%V\" "
                          "for key \"%V\"",
                          &line, &ctx->key);
            return NGX_HTTP_UPSTREAM_INVALID_HEADER;
        }

        /*
         * ���flags & mlcf->gzip_flagΪ�棬��ʾmemcached���������ص���Ӧ���������Ǿ���ѹ���ģ�
         * �����ڽ�ѹ������Ӧ���巢�͸��ͻ���֮ǰ����Ҫ�ڷ��͸��ͻ��˵�http��Ӧͷ��������
         * ���ݱ���"Content-Encoding"�ֶε�ֵΪgzip���������ͻ��˽��յ�Nginx���͵���Ӧ����֮��
         * ������ȷ�Ľ��н��롣
         */
        if (flags & mlcf->gzip_flag) {

            /*
             * �����ݱ����ֶ�"Content-Encoding"����ֵ"gzip"���õ�r->headers_out.headers��������
             * r->headers_out.headers�е��������л��ɷ��͸��ͻ��˵���Ӧͷ��ʱҲ�Ͱ��������ݱ���
             * �ֶμ���ֵ�ˡ�
             */
            h = ngx_list_push(&r->headers_out.headers);
            if (h == NULL) {
                return NGX_ERROR;
            }

            h->hash = 1;
            ngx_str_set(&h->key, "Content-Encoding");
            ngx_str_set(&h->value, "gzip");
            r->headers_out.content_encoding = h;
        }

    length:
    
        /* ����Ӧͷ���л�ȡ��Ӧ����ĳ��� */

        /* ��ʱpָ����Ǵ������Ӧ���峤�ȵ��ڴ棬���������ø��˾ֲ�����start�� */
        start = p;

        /*
         * ָ�������������֮��pָ������Ӧͷ��ĩβ����ôp - start������Ӧͷ����
         * ָʾ���峤�ȵ��ֶ���ռ�õ��ڴ泤����
         */
        p = line.data + line.len;

        /* ������Ӧͷ������ȡ��Ӧ���峤�� */
        u->headers_in.content_length_n = ngx_atoof(start, p - start);
        if (u->headers_in.content_length_n == NGX_ERROR) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "memcached sent invalid length in response \"%V\" "
                          "for key \"%V\"",
                          &line, &ctx->key);
            return NGX_HTTP_UPSTREAM_INVALID_HEADER;
        }

        /*
         * ��Ϊ����ִ�е��������memcached���������ص���Ӧͷ���Ѿ��ɹ��������ˣ�
         * ����ͷ��Ҳ�ǺϷ��ġ�����������Ҫ����һЩ״̬��־
         */
        u->headers_in.status_n = 200;
        u->state->status = 200;
        /* ִ�������������u->buffer.posָ�������Ӧͷ����һ��λ�ã�Ҳ���ǰ������ݵĿ�ʼλ�� */
        u->buffer.pos = p + sizeof(CRLF) - 1;

        return NGX_OK;
    }

    /* �����Ӧͷ����"END\x0d"������memcachedģ�鷢�͸�memcached��������key��Ӧ��ֵ��memcached�������в����� */
    if (ngx_strcmp(p, "END\x0d") == 0) {
        ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                      "key: \"%V\" was not found by memcached", &ctx->key);

        /* ��Ϊ��memcached��������û���ҵ�key��Ӧ��ֵ��������Ҫ����һЩ״̬ */
        u->headers_in.content_length_n = 0;
        u->headers_in.status_n = 404;
        u->state->status = 404;
        u->buffer.pos = p + sizeof("END" CRLF) - 1;
        u->keepalive = 1;

        return NGX_OK;
    }

    /*
     * �����Ӧͷ�����ݼȲ�����"VALUE "��ͷ��Ҳ����"END\x0d"����ô�����Ӧ�����ǷǷ��ģ�
     * ���ʱ��᷵��NGX_HTTP_UPSTREAM_INVALID_HEADER����ʾ�ӷ��������յ�����Ӧͷ���ǷǷ��ġ�
     */

no_valid:

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "memcached sent invalid response: \"%V\"", &line);

    return NGX_HTTP_UPSTREAM_INVALID_HEADER;
}

/* Ϊ���������memcached���������յ�������׼�� */
static ngx_int_t
ngx_http_memcached_filter_init(void *data)
{
    ngx_http_memcached_ctx_t  *ctx = data;

    ngx_http_upstream_t  *u;

    u = ctx->request->upstream;

    /*
     * ������η��������ص���Ӧͷ�е���Ӧ״̬����404����ô������ʣ��δ���յ���Ӧ���峤�ȵ�u->length��
     * ����ʼ��memcachedģ�������ĵ�rest�ֶ�ΪNGX_HTTP_MEMCACHED_END����ʾʣ��δ���յı�־����memcached
     * ��������Ч��Ӧ�����Ѿ������ı�־�еĳ���
     */
    if (u->headers_in.status_n != 404) {
        u->length = u->headers_in.content_length_n + NGX_HTTP_MEMCACHED_END;
        ctx->rest = NGX_HTTP_MEMCACHED_END;

    } else {
        u->length = 0;
    }

    return NGX_OK;
}

/* ���������memcached���������յ��İ������� */
static ngx_int_t
ngx_http_memcached_filter(void *data, ssize_t bytes)
{
    ngx_http_memcached_ctx_t  *ctx = data;

    u_char               *last;
    ngx_buf_t            *b;
    ngx_chain_t          *cl, **ll;
    ngx_http_upstream_t  *u;

    /*
     * �Ӵ���memcachedģ��loc������������ṹ��ĺ���ngx_http_memcached_create_loc_conf()������
     * ���Կ�����memcachedģ���ʹ�ù̶���С�Ļ���������memcached���������͹�������Ӧ���ݣ�������
     * ����upstream�����е�buffer��
     */
    u = ctx->request->upstream;
    b = &u->buffer;

    /*
     * ���u->length == (ssize_t) ctx->rest�����Ļ��������Ѿ���ʼ���ձ�־memcached��������Ч��Ӧ���������
     * ��־��(CRLF "END" CRLF)�������ˣ�����һ�����ξ��������յ���ʣ��δ���ձ�־�е�ȫ�����ݡ�
     */
    if (u->length == (ssize_t) ctx->rest) {

        /* �Ƚϱ��ν��յ���ʣ�ಿ���Ƿ��ʣ��δ���յı�־������һ�� */
        if (ngx_strncmp(b->last,
                   ngx_http_memcached_end + NGX_HTTP_MEMCACHED_END - ctx->rest,
                   bytes)
            != 0)
        {
            ngx_log_error(NGX_LOG_ERR, ctx->request->connection->log, 0,
                          "memcached sent invalid trailer");

            u->length = 0;
            ctx->rest = 0;

            return NGX_OK;
        }

        /* ����ʣ��δ���յİ��峤�� */
        u->length -= bytes;
        ctx->rest -= bytes;

        /* ���u->length����0����ʾ���յ�����������memcached��������ȫ����Ӧ�������ݣ�������־�е����� */
        if (u->length == 0) {
            u->keepalive = 1;
        }

        return NGX_OK;
    }

    /*
     * ��ʹ�ù̶���С�Ļ����������memcached���������͹�������Ӧʱ��������Ϊupstream�����е�
     * buffer�ֶΣ�����ÿ�ν��յ��Ĵ����buffer�е���Ӧ����Σ�����һ��û�з���ʵ���ڴ�Ļ�����
     * �����������ν��յ��İ������ݣ�Ȼ������������������ص�upstream�����out_bufs������
     * ��������е�ĩβ��
     */
    /*
     * ѭ������u->out_bufs������������λ�������β��
     */
    for (cl = u->out_bufs, ll = &u->out_bufs; cl; cl = cl->next) {
        ll = &cl->next;
    }

    /* ��u->free_bufs�л�ȡһ�����еĻ������������������ν��յ�����������memcached�������İ��� */
    cl = ngx_chain_get_free_buf(ctx->request->pool, &u->free_bufs);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    /*
     * ���û����������е�flush��memory��־λ���ֱ��ʾ��Ҫ�����Լ�����������Ķ������ڴ��С�
     */
    cl->buf->flush = 1;
    cl->buf->memory = 1;

    *ll = cl;

    /* b->lastָ����Ǳ��ν��յ��İ���ĵ�ַ�����������ø��ֲ�����last */
    last = b->last;
    cl->buf->pos = last;  // ��������posָ��ָ��������ʼ��ַ��posһ��ָʾδ������ڴ�Ŀ�ʼ
    b->last += bytes;  // b->lastƫ�Ƶ����ν��յ��İ������һ��λ�ã�Ϊ�������հ�����׼��
    cl->buf->last = b->last;  // ��������lastָ��ָ���˱��ν��յİ���ĵ���һ��λ�ã���pos��ͬ������İ���
    cl->buf->tag = u->output.tag;

    ngx_log_debug4(NGX_LOG_DEBUG_HTTP, ctx->request->connection->log, 0,
                   "memcached filter bytes:%z size:%z length:%O rest:%z",
                   bytes, b->last - b->pos, u->length, ctx->rest);

    /*
     * ���bytes <= (ssize_t) (u->length - NGX_HTTP_MEMCACHED_END)�����Ļ�����������Ҫ�����η���������
     * ����İ��壬���ʱ�����upstream�й����ʣ��δ���յİ��峤�ȣ�����NGX_OK���ȴ��´ν��յ������
     * �ٵ��ñ��������д���
     * ��bytes == (ssize_t) (u->length - NGX_HTTP_MEMCACHED_END)���������ָ��Ч�����Ѿ���������ˣ�����
     * ���б�־��Ч��������ı�־�л�û�н��յ�����˻���Ҫ�����ⲿ�����ݡ����ʱ�������u->length֮��
     * �ͻ����u->length == ctx->rest������ˡ�
     */
    if (bytes <= (ssize_t) (u->length - NGX_HTTP_MEMCACHED_END)) {
        u->length -= bytes;
        return NGX_OK;
    }

    /*
     * ����ִ�е�����������ν�����֮���Ѿ�������memcached���������յ�����������Ч��Ӧ���壬���ǿ��ܻ�û��
     * �������յ�����ָʾmemcached��������Ӧ�����Ѿ������ı�־��(CRLF "END" CRLF)��������Ҫ����һ�����жϡ�
     */

    /* �Ƚ�lastָ�붨λ����־��Ч��Ӧ�����Ѿ������ı�־�еĿ�ʼλ�� */
    last += (size_t) (u->length - NGX_HTTP_MEMCACHED_END);

    /*
     * �ж��Ѿ����յı�־��Ч��Ӧ��������ı�־���Ƿ���Ч��(���ʱ��һ���������ģ�ע�⵽�����������ĳ���
     * �����Ǳ�־�е��ܳ��ȣ������Ѿ����յ��˿����ǲ��ֱ�־�еĳ���) 
     */
    if (ngx_strncmp(last, ngx_http_memcached_end, b->last - last) != 0) {
        ngx_log_error(NGX_LOG_ERR, ctx->request->connection->log, 0,
                      "memcached sent invalid trailer");

        b->last = last;
        cl->buf->last = last;
        u->length = 0;
        ctx->rest = 0;

        return NGX_OK;
    }

    /*
     * ����ִ�е������ʾ���յ���������־memcached��������Ӧ��������ı�־�п��ܻ�û�н���������������Ҫ��¼
     * ��ǰ�����״̬��(������־memcached��������Ч��Ӧ��������ı�־�е����ݶ���Nginx��˵��û��ʲô�ã�����
     * ��Ȼ�б�Ҫ���գ����Կ��Ը����ⲿ�����ݣ��������b->last = last���Կ���������ʣ��δ���յı�־������
     * ��ֱ�Ӹ����Ѿ����յ��Ĳ��ֱ�־�У���Ϊb->last��ʾ�����´ν��հ������ʼλ��)�������ⲿ�����ݲ�����
     * ���ص�out_bufs�����������У�Ҳ�Ͳ��ᷢ�͸��ͻ����ˡ�
     */
    ctx->rest -= b->last - last;  // ��¼��־��ʣ��δ���յĳ���
    b->last = last;  // ��u->buffer�е�lastָ������Ϊ��־����ʼ��ַ
    cl->buf->last = last;  //���ڹ����ν��յ��İ���Ļ����������lastָ��ָ������Ч�������һ����ַ������־�п�ʼ
    u->length = ctx->rest;  // ��ʣ��δ���յİ��峤������Ϊʣ��δ���ձ�־�еĳ��ȡ�

    /* ���u->length����0����ʾ���յ�����������memcached��������ȫ����Ӧ�������ݣ�������־�е����ݡ� */
    if (u->length == 0) {
        u->keepalive = 1;
    }

    return NGX_OK;
}


static void
ngx_http_memcached_abort_request(ngx_http_request_t *r)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "abort http memcached request");
    return;
}


static void
ngx_http_memcached_finalize_request(ngx_http_request_t *r, ngx_int_t rc)
{
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "finalize http memcached request");
    return;
}

/* �������ڴ��memcachedģ��loc�����µ�������ṹ�� */
static void *
ngx_http_memcached_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_memcached_loc_conf_t  *conf;

    /* �������ڴ��memcachedģ��loc���������������ṹ���ڴ� */
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_memcached_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->upstream.bufs.num = 0;
     *     conf->upstream.next_upstream = 0;
     *     conf->upstream.temp_path = NULL;
     *     conf->upstream.uri = { 0, NULL };
     *     conf->upstream.location = NULL;
     */

    conf->upstream.local = NGX_CONF_UNSET_PTR;
    conf->upstream.next_upstream_tries = NGX_CONF_UNSET_UINT;
    conf->upstream.connect_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.send_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.read_timeout = NGX_CONF_UNSET_MSEC;
    conf->upstream.next_upstream_timeout = NGX_CONF_UNSET_MSEC;

    conf->upstream.buffer_size = NGX_CONF_UNSET_SIZE;

    /* the hardcoded values */
    /*
     * �����ǰ�memcachedģ���õ���upstream���Ƶ�һЩ��Ա����Ӳ���룬��ʾĳЩ���ܵĹ̻���
     * ���罫buffering����Ϊ0������ʹ�ù̶���С���ڴ�����������memcached���������͹�����
     * ��Ӧ�����ʱ��Ҳ�Ͳ���Ҫ������ڴ滺�����Ѿ���ʱ�ļ�������Ҳ�Ὣ��ص��ֶν������á�
     */
    conf->upstream.cyclic_temp_file = 0;
    conf->upstream.buffering = 0;
    conf->upstream.ignore_client_abort = 0;
    conf->upstream.send_lowat = 0;
    conf->upstream.bufs.num = 0;
    conf->upstream.busy_buffers_size = 0;
    conf->upstream.max_temp_file_size = 0;
    conf->upstream.temp_file_write_size = 0;
    conf->upstream.intercept_errors = 1;
    conf->upstream.intercept_404 = 1;
    conf->upstream.pass_request_headers = 0;
    conf->upstream.pass_request_body = 0;
    conf->upstream.force_ranges = 1;

    conf->index = NGX_CONF_UNSET;
    conf->gzip_flag = NGX_CONF_UNSET_UINT;

    return conf;
}


static char *
ngx_http_memcached_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_memcached_loc_conf_t *prev = parent;
    ngx_http_memcached_loc_conf_t *conf = child;

    ngx_conf_merge_ptr_value(conf->upstream.local,
                              prev->upstream.local, NULL);

    ngx_conf_merge_uint_value(conf->upstream.next_upstream_tries,
                              prev->upstream.next_upstream_tries, 0);

    ngx_conf_merge_msec_value(conf->upstream.connect_timeout,
                              prev->upstream.connect_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.send_timeout,
                              prev->upstream.send_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.read_timeout,
                              prev->upstream.read_timeout, 60000);

    ngx_conf_merge_msec_value(conf->upstream.next_upstream_timeout,
                              prev->upstream.next_upstream_timeout, 0);

    ngx_conf_merge_size_value(conf->upstream.buffer_size,
                              prev->upstream.buffer_size,
                              (size_t) ngx_pagesize);

    ngx_conf_merge_bitmask_value(conf->upstream.next_upstream,
                              prev->upstream.next_upstream,
                              (NGX_CONF_BITMASK_SET
                               |NGX_HTTP_UPSTREAM_FT_ERROR
                               |NGX_HTTP_UPSTREAM_FT_TIMEOUT));

    if (conf->upstream.next_upstream & NGX_HTTP_UPSTREAM_FT_OFF) {
        conf->upstream.next_upstream = NGX_CONF_BITMASK_SET
                                       |NGX_HTTP_UPSTREAM_FT_OFF;
    }

    if (conf->upstream.upstream == NULL) {
        conf->upstream.upstream = prev->upstream.upstream;
    }

    if (conf->index == NGX_CONF_UNSET) {
        conf->index = prev->index;
    }

    ngx_conf_merge_uint_value(conf->gzip_flag, prev->gzip_flag, 0);

    return NGX_CONF_OK;
}

/* memcached_passָ�����������memcached_pass��������Я��һ��url���� */
static char *
ngx_http_memcached_pass(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_memcached_loc_conf_t *mlcf = conf;

    ngx_str_t                 *value;
    ngx_url_t                  u;
    ngx_http_core_loc_conf_t  *clcf;

    if (mlcf->upstream.upstream) {
        return "is duplicate";
    }

    /* ��ȡurl���� */
    value = cf->args->elts;

    ngx_memzero(&u, sizeof(ngx_url_t));

    u.url = value[1];
    u.no_resolve = 1;  // urlû�н���dns�����ı�־λ

    /*
     * ��ȡһ���洢upstream���ÿ���Ϣ�Ľṹ�壬���ʱ��һ�㲻�ᴴ��һ���µ�upstream���ÿ飬
     * ���Ǵ����е�upstream���ÿ����ҳ���memcached_pass�������ƥ����Ǹ�upstream���ÿ飬����
     * ��Ҫʹ�õ����upstream���ÿ��е���Ϣ���������������ؾ���֮��ġ�proxy_pass����Ҳ������
     */
    mlcf->upstream.upstream = ngx_http_upstream_add(cf, &u, 0);
    if (mlcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }

    /* clcf��������location */
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    /*
     * ����clcf->handler�ص������������ngx_http_core_content_phase���checker�����оͻ�����������
     * ����NGX_HTTP_CONTENT_PHASE�׶εĴ���
     * ��ngx_http_update_location_config()�����лὫclcf->handler��ֵ��r->content_hander��
     */
    clcf->handler = ngx_http_memcached_handler;

    /* ���location��'/'��β������Ҫ�����ض��� */
    if (clcf->name.data[clcf->name.len - 1] == '/') {
        clcf->auto_redirect = 1;
    }

    /* ��ȡ�����ļ������õ�memcached_key�������±꣬�����mlcf->index */
    mlcf->index = ngx_http_get_variable_index(cf, &ngx_http_memcached_key);

    if (mlcf->index == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

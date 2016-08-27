
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


static void ngx_http_read_client_request_body_handler(ngx_http_request_t *r);
static ngx_int_t ngx_http_do_read_client_request_body(ngx_http_request_t *r);
static ngx_int_t ngx_http_write_request_body(ngx_http_request_t *r);
static ngx_int_t ngx_http_read_discarded_request_body(ngx_http_request_t *r);
static ngx_int_t ngx_http_discard_request_body_filter(ngx_http_request_t *r,
    ngx_buf_t *b);
static ngx_int_t ngx_http_test_expect(ngx_http_request_t *r);

static ngx_int_t ngx_http_request_body_filter(ngx_http_request_t *r,
    ngx_chain_t *in);
static ngx_int_t ngx_http_request_body_length_filter(ngx_http_request_t *r,
    ngx_chain_t *in);
static ngx_int_t ngx_http_request_body_chunked_filter(ngx_http_request_t *r,
    ngx_chain_t *in);

/* ��������������� */
ngx_int_t
ngx_http_read_client_request_body(ngx_http_request_t *r,
    ngx_http_client_body_handler_pt post_handler)
{
    size_t                     preread;
    ssize_t                    size;
    ngx_int_t                  rc;
    ngx_buf_t                 *b;
    ngx_chain_t                out;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    /* �Ƚ�����������ü�����1����ʾ������������һ������ */
    r->main->count++;

    /*
     * 1.�����������ԭʼ��������Ҫ���տͻ���������壬��Ϊ�������ǿͻ��˲����ġ�
     * 2.��������е�request_body��Ա������ó�Ա�Ѿ���������ˣ�֤��֮ǰ�Ѿ���ȡ����������
     * �����ٶ�ȡһ�顣
     * 3.��������е�discard_body��־λΪ1������֮ǰ�Ѿ�ִ�й���������ķ�����Ҳ�����ټ���
     * ��ȡ�������ˡ�
     */
    if (r != r->main || r->request_body || r->discard_body) {
        r->request_body_no_buffering = 0;
        post_handler(r);
        return NGX_OK;
    }

#if (NGX_HTTP_V2)
    if (r->stream) {
        rc = ngx_http_v2_read_request_body(r, post_handler);
        goto done;
    }
#endif

    /* ���ͻ��˷��͵�����ͷ�����Ƿ���Expectͷ�� */
    if (ngx_http_test_expect(r) != NGX_OK) {
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
        goto done;
    }

    /* �������ڽ���������Ķ��� */
    rb = ngx_pcalloc(r->pool, sizeof(ngx_http_request_body_t));
    if (rb == NULL) {
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
        goto done;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     rb->bufs = NULL;
     *     rb->buf = NULL;
     *     rb->free = NULL;
     *     rb->busy = NULL;
     *     rb->chunked = NULL;
     */

    rb->rest = -1;
    rb->post_handler = post_handler;  // ���ð����ȡ��ϵĻص�������ͨ������ʵ��ģ���ҵ���߼�

    r->request_body = rb;

    /* ���ģ���Content-Lengthͷ��ֵС��0�����ý���������� */
    if (r->headers_in.content_length_n < 0 && !r->headers_in.chunked) {
        r->request_body_no_buffering = 0;
        post_handler(r);
        return NGX_OK;
    }

    /*
     * �ڽ�������ͷ���������У����п��ܽ��յ�http�������ģ�����������Ҫ������ͷ���Ļ�����
     * ���Ƿ�Ԥ���յ��˰��塣����֪��header_in->last��header_in->pos֮����ڴ����Ϊ�������ַ�����
     * preread�������0����ʾȷʵԤ���յ��˰���
     */
    preread = r->header_in->last - r->header_in->pos;

    if (preread) {

        /* there is the pre-read part of the request body */

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http client request body preread %uz", preread);

        /* ����������ָ��ͷ��������header_in */
        out.buf = r->header_in;
        out.next = NULL;

        /* �ú����л����ʣ��δ���յİ��峤�� */
        rc = ngx_http_request_body_filter(r, &out);

        if (rc != NGX_OK) {
            goto done;
        }

        /* ���㵽Ŀǰλ���Ѿ����յ�������ĳ��� */
        r->request_length += preread - (r->header_in->last - r->header_in->pos);

        /*
         * �������ʣ��δ���յİ��壬����ʣ�����ĳ���С��ͷ��������ʣ�೤�ȣ���ô
         * ����ʹ��ͷ��������������ʣ��İ���
         */
        if (!r->headers_in.chunked
            && rb->rest > 0
            && rb->rest <= (off_t) (r->header_in->end - r->header_in->last))
        {
            /* the whole request body may be placed in r->header_in */

            b = ngx_calloc_buf(r->pool);
            if (b == NULL) {
                rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
                goto done;
            }

            /* ���建����ֱ��ָ����ͷ����������Ӧ���ڴ� */
            b->temporary = 1;
            b->start = r->header_in->pos;
            b->pos = r->header_in->pos;
            b->last = r->header_in->last;
            b->end = r->header_in->end;

            rb->buf = b;

            /*
             * ��Ϊ�����������Ķ��������޷���һ�ε�������ɣ�������Ҫ����������¼���������
             * ���������Ӷ�Ӧ�Ķ��¼��ٴα�epoll����ʱ�����Լ���ִ�н��հ���Ķ�����
             */
            r->read_event_handler = ngx_http_read_client_request_body_handler;
            r->write_event_handler = ngx_http_request_empty_handler;

            /* �����Ӷ�Ӧ���ں��׽��ֻ������ж�ȡ���� */
            rc = ngx_http_do_read_client_request_body(r);
            goto done;
        }

    } else {
        /* set rb->rest */

        /* ����ʣ��δ���յ��������ĳ��ȣ���rb->rest */
        if (ngx_http_request_body_filter(r, NULL) != NGX_OK) {
            rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
            goto done;
        }
    }

    /* rb->rest == 0�����Ѿ����յ����������������(��ʵ����ͷ���������о�Ԥ��ȡ����������) */
    if (rb->rest == 0) {
        /* the whole request body was pre-read */
        r->request_body_no_buffering = 0;
        post_handler(r);
        return NGX_OK;
    }

    if (rb->rest < 0) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                      "negative request body rest");
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
        goto done;
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

    /*
     * ����ִ�е�����˵����Ҫ�������ڽ����������Ļ������ˣ��������ĳ����������ļ��е�
     * client_body_buffer_size������ָ����
     */
    size = clcf->client_body_buffer_size;
    size += size >> 2;

    /* TODO: honor r->request_body_in_single_buf */

    /* rb->rest < size����ʣ��δ���հ����ò���size���ȣ����Է���rb->rest���Ⱦ͹��� */
    if (!r->headers_in.chunked && rb->rest < size) {
        size = (ssize_t) rb->rest;

        /*
         * ���r->request_body_in_single_buf��־λΪ1��������Ҫ�����е������������һ�黺�����У�
         * ���ʱ����Ҫ��ͷ����������Ԥ��ȡ�İ���һ�����ƹ����������ڼ������ڽ����������Ļ��������ȵ�ʱ��
         * ��ҪΪ�Ѿ������ͷ���������İ��������Ӧ���ڴ棬��Ϊ�ǲ��ְ���ҲҪ���Ƶ��û�������
         */
        if (r->request_body_in_single_buf) {
            size += preread;
        }

    } else {
        size = clcf->client_body_buffer_size;
    }

    /* �������ڽ��հ���Ļ����� */
    rb->buf = ngx_create_temp_buf(r->pool, size);
    if (rb->buf == NULL) {
        rc = NGX_HTTP_INTERNAL_SERVER_ERROR;
        goto done;
    }

    /* ���������д�¼� */
    /*
     * ��Ϊ�����������Ķ��������޷���һ�ε�������ɣ�������Ҫ����������¼���������
     * ���������Ӷ�Ӧ�Ķ��¼��ٴα�epoll����ʱ�����Լ���ִ�н��հ���Ķ�����
     */
    r->read_event_handler = ngx_http_read_client_request_body_handler;
    r->write_event_handler = ngx_http_request_empty_handler;

    /* ���հ��� */
    rc = ngx_http_do_read_client_request_body(r);

done:

    if (r->request_body_no_buffering
        && (rc == NGX_OK || rc == NGX_AGAIN))
    {
        if (rc == NGX_OK) {
            r->request_body_no_buffering = 0;

        } else {
            /* rc == NGX_AGAIN */
            r->reading_body = 1;  // NGX_AGAIN�������ڶ�ȡ�������
        }

        r->read_event_handler = ngx_http_block_reading;
        post_handler(r);
    }

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        r->main->count--;
    }

    return rc;
}


ngx_int_t
ngx_http_read_unbuffered_request_body(ngx_http_request_t *r)
{
    ngx_int_t  rc;

#if (NGX_HTTP_V2)
    if (r->stream) {
        rc = ngx_http_v2_read_unbuffered_request_body(r);

        if (rc == NGX_OK) {
            r->reading_body = 0;
        }

        return rc;
    }
#endif

    if (r->connection->read->timedout) {
        r->connection->timedout = 1;
        return NGX_HTTP_REQUEST_TIME_OUT;
    }

    rc = ngx_http_do_read_client_request_body(r);

    if (rc == NGX_OK) {
        r->reading_body = 0;
    }

    return rc;
}


static void
ngx_http_read_client_request_body_handler(ngx_http_request_t *r)
{
    ngx_int_t  rc;

    /*
     * ���read->timedoutΪ1���������ȡ������峬ʱ����ʱ��Ҫ�������ϵ�timeout��λ��
     * �������󲢷���408������Ӧ
     */
    if (r->connection->read->timedout) {
        r->connection->timedout = 1;
        ngx_http_finalize_request(r, NGX_HTTP_REQUEST_TIME_OUT);
        return;
    }

    /* ��ȡ������� */
    rc = ngx_http_do_read_client_request_body(r);

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        ngx_http_finalize_request(r, rc);
    }
}

/*
 * ��ȡ���壬��������:
 * 1.�ѿͻ��˺�Nginx֮���tcp�����ϵ��ں��׽��ֻ������е��ַ���������
 * 2.�ж��ַ����Ƿ���Ҫд���ļ����Լ��Ƿ���յ���ȫ�����������
 * 3.�ڽ��յ�ȫ�����������󼤻�����ִ�ж�ȡ��������ģ��ҵ���߼��ĺ���post_handler()
 */
static ngx_int_t
ngx_http_do_read_client_request_body(ngx_http_request_t *r)
{
    off_t                      rest;
    size_t                     size;
    ssize_t                    n;
    ngx_int_t                  rc;
    ngx_chain_t                out;
    ngx_connection_t          *c;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    /* ��ȡ���Ӷ���ʹ洢������� */
    c = r->connection;
    rb = r->request_body;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http read client request body");

    for ( ;; ) {
        for ( ;; ) {
            if (rb->buf->last == rb->buf->end) {

                if (rb->buf->pos != rb->buf->last) {

                    /* pass buffer to request body filter chain */

                    out.buf = rb->buf;
                    out.next = NULL;

                    rc = ngx_http_request_body_filter(r, &out);

                    if (rc != NGX_OK) {
                        return rc;
                    }

                } else {

                    /* update chains */

                    rc = ngx_http_request_body_filter(r, NULL);

                    if (rc != NGX_OK) {
                        return rc;
                    }
                }

                if (rb->busy != NULL) {
                    if (r->request_body_no_buffering) {
                        if (c->read->timer_set) {
                            ngx_del_timer(c->read);
                        }

                        if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
                            return NGX_HTTP_INTERNAL_SERVER_ERROR;
                        }

                        return NGX_AGAIN;
                    }

                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }

                rb->buf->pos = rb->buf->start;
                rb->buf->last = rb->buf->start;
            }

            size = rb->buf->end - rb->buf->last;
            rest = rb->rest - (rb->buf->last - rb->buf->pos);

            if ((off_t) size > rest) {
                size = (size_t) rest;
            }

            n = c->recv(c, rb->buf->last, size);

            ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                           "http client request body recv %z", n);

            if (n == NGX_AGAIN) {
                break;
            }

            if (n == 0) {
                ngx_log_error(NGX_LOG_INFO, c->log, 0,
                              "client prematurely closed connection");
            }

            if (n == 0 || n == NGX_ERROR) {
                c->error = 1;
                return NGX_HTTP_BAD_REQUEST;
            }

            rb->buf->last += n;
            r->request_length += n;

            if (n == rest) {
                /* pass buffer to request body filter chain */

                out.buf = rb->buf;
                out.next = NULL;

                rc = ngx_http_request_body_filter(r, &out);

                if (rc != NGX_OK) {
                    return rc;
                }
            }

            if (rb->rest == 0) {
                break;
            }

            if (rb->buf->last < rb->buf->end) {
                break;
            }
        }

        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, c->log, 0,
                       "http client request body rest %O", rb->rest);

        if (rb->rest == 0) {
            break;
        }

        if (!c->read->ready) {

            if (r->request_body_no_buffering
                && rb->buf->pos != rb->buf->last)
            {
                /* pass buffer to request body filter chain */

                out.buf = rb->buf;
                out.next = NULL;

                rc = ngx_http_request_body_filter(r, &out);

                if (rc != NGX_OK) {
                    return rc;
                }
            }

            clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);
            ngx_add_timer(c->read, clcf->client_body_timeout);

            if (ngx_handle_read_event(c->read, 0) != NGX_OK) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            return NGX_AGAIN;
        }
    }

    if (c->read->timer_set) {
        ngx_del_timer(c->read);
    }

    if (!r->request_body_no_buffering) {
        r->read_event_handler = ngx_http_block_reading;
        rb->post_handler(r);
    }

    return NGX_OK;
}


static ngx_int_t
ngx_http_write_request_body(ngx_http_request_t *r)
{
    ssize_t                    n;
    ngx_chain_t               *cl, *ln;
    ngx_temp_file_t           *tf;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    rb = r->request_body;

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http write client request body, bufs %p", rb->bufs);

    if (rb->temp_file == NULL) {
        tf = ngx_pcalloc(r->pool, sizeof(ngx_temp_file_t));
        if (tf == NULL) {
            return NGX_ERROR;
        }

        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        tf->file.fd = NGX_INVALID_FILE;
        tf->file.log = r->connection->log;
        tf->path = clcf->client_body_temp_path;
        tf->pool = r->pool;
        tf->warn = "a client request body is buffered to a temporary file";
        tf->log_level = r->request_body_file_log_level;
        tf->persistent = r->request_body_in_persistent_file;
        tf->clean = r->request_body_in_clean_file;

        if (r->request_body_file_group_access) {
            tf->access = 0660;
        }

        rb->temp_file = tf;

        if (rb->bufs == NULL) {
            /* empty body with r->request_body_in_file_only */

            if (ngx_create_temp_file(&tf->file, tf->path, tf->pool,
                                     tf->persistent, tf->clean, tf->access)
                != NGX_OK)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }
    }

    if (rb->bufs == NULL) {
        return NGX_OK;
    }

    n = ngx_write_chain_to_temp_file(rb->temp_file, rb->bufs);

    /* TODO: n == 0 or not complete and level event */

    if (n == NGX_ERROR) {
        return NGX_ERROR;
    }

    rb->temp_file->offset += n;

    /* mark all buffers as written */

    for (cl = rb->bufs; cl; /* void */) {

        cl->buf->pos = cl->buf->last;

        ln = cl;
        cl = cl->next;
        ngx_free_chain(r->pool, ln);
    }

    rb->bufs = NULL;

    return NGX_OK;
}

/*
 * ��һ�������������嶯�� 
 *     ����httpģ����ԣ��������հ�����Ǽ򵥵ز����հ��壬���Ƕ���http�����˵������
 * �����հ���Ϳ��Եġ���Ϊ�ͻ���ͨ�������һЩ�������������Ͱ��壬���http���
 * һֱ�����հ��壬�ᵼ��ʵ���ϲ�����׳�Ŀͻ�����Ϊ��������ʱ����Ӧ�������ӹرգ�
 * �������ʱ��Nginx���ܻ��ڴ���������ӣ������ͻᵼ�³���
 *     ����httpģ��������հ��壬��http�����˵���ǽ��հ��壬�����պ󲻱��棬ֱ�Ӷ���
 */
ngx_int_t
ngx_http_discard_request_body(ngx_http_request_t *r)
{
    ssize_t       size;
    ngx_int_t     rc;
    ngx_event_t  *rev;

    /*
     * 1. ��鵱ǰ�����ǲ��������������������Ļ����Ͳ��ô�����壬��Ϊ�����󲢲�������
     * �ͻ��˵��������Բ����ڴ���http�������ĸ�����������������ֱ�ӷ���NGX_OK��ʾ�����ɹ�
     * 2. ��������е�discard_body��־λ������ñ�־λΪ1����ʾ�Ѿ���ִ�ж����Ķ�������������
     * ֱ�ӷ��ء�
     * 3. ��������е�request_body���������NULL��˵��֮ǰģ��ִ�й���ȡ����Ķ�������������
     * ������ִ�ж�������Ķ����ˡ�
     */
    if (r != r->main || r->discard_body || r->request_body) {
        return NGX_OK;
    }

#if (NGX_HTTP_V2)
    if (r->stream) {
        r->stream->skip_data = 1;
        return NGX_OK;
    }
#endif

    /* ���Expectͷ������������Ӧ������ͻ��˷��������� */
    if (ngx_http_test_expect(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    rev = r->connection->read;

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, rev->log, 0, "http set discard body");

    /* 
     * ��鵱ǰ���ӵĶ��¼��Ƿ��ڶ�ʱ���У�����ڣ���Ӷ�ʱ����ɾ������Ϊ��������
     * ���ÿ��ǳ�ʱ�����⡣������һ������»Ὣ���Ӷ��¼����¼��뵽��ʱ�����У��Ǿ���
     * ��Nginx�Ѿ�������������ǿͻ��˻�û�н����еİ��巢����ϣ����ʱ�����Ҫ
     * �����Ӷ��¼����綨ʱ����������ʱ����ʱʱ������Ϊlingering_timeout���������
     * ����ngx_http_finalize_connection()��������ɵģ������������ʱ���ֿͻ���
     * ��û�з����������壬�ͻὫ���Ӷ��¼����뵽��ʱ���С�
     */
    if (rev->timer_set) {
        ngx_del_timer(rev);
    }

    /* �������ͷ��"Content-Length"ָ���ĳ���С�ڵ���0����ֱ�ӷ��أ�����ִ�ж������� */
    if (r->headers_in.content_length_n <= 0 && !r->headers_in.chunked) {
        return NGX_OK;
    }

    /* ������http����ͷ���Ļ��������Ƿ��Ѿ�Ԥ���յ���������� */
    size = r->header_in->last - r->header_in->pos;

    /* ���ͷ�����������Ѿ����յ���������壬�����Ƿ��Ѿ����յ���ȫ����������� */
    if (size || r->headers_in.chunked) {
        rc = ngx_http_discard_request_body_filter(r, r->header_in);  // ����ʣ��δ���հ��峤��

        if (rc != NGX_OK) {
            return rc;
        }

        /*
         * ���������ʱ�򣬻�ʹ����������е�r->headers_in.content_length_n����ʾʣ��δ���հ���ĳ���
         */

        /* ���ʣ��δ���հ��峤��Ϊ0��Ҳ���ǽ��յ�������������壬���ʾ��������ִ�гɹ�������NGX_OK */
        if (r->headers_in.content_length_n == 0) {
            return NGX_OK;
        }
    }

    /* ��ȡ���� */
    rc = ngx_http_read_discarded_request_body(r);

    /* ngx_http_read_discarded_request_body����NGX_OK��ʾ��ȡ����Ķ��������ˣ����������ٶ�ȡ������ */
    if (rc == NGX_OK) {
        r->lingering_close = 0;  // �������ӳٹرյı�־λ���㣬��ʾ������Ϊ���հ�����ӳٹر���
        return NGX_OK;
    }

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    /*
     *     ����NGX_AGAIN������Ҫ�¼�ģ���ε��Ȳ�����ɶ��������������Ķ��������ʱ����Ҫ������Ķ��¼�
     * ����������Ϊngx_http_discarded_request_body_handler�������������ж��¼�ʱ�����øú���������ȡ���塣
     * ������֮���¼����뵽epoll�н��м�ء�
     *     �������ö��¼��������ͼ�ض��¼�֮�⣬����Ҫ����������е�discard_body��λ����ʾ��ǰ���ڽ���
     * ��������Ķ�����ͬʱ������������ü�����1����ֹNginx���������󣬵��ǿͻ��˻�û�з�������嵼��Nginx
     * �ͷ��������������������⣬����������£�Nginx�ڽ�������ʱ���ֵ�ǰ�����ڽ��ж�������Ķ���������
     * Nginx�Ὣ���Ӷ��¼����뵽��ʱ���У����ӳٹر����󣬼�ngx_http_finalize_connection������ӳ�ʱ�����
     * ��ʱ����ʱ���򲻹��Ƿ���յ��������������壬Ҳ���ͷ����󣬼�ngx_http_discarded_request_body_handler��
     */
    /* rc == NGX_AGAIN */

    r->read_event_handler = ngx_http_discarded_request_body_handler;

    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    /* ���������ü�����1����λdiscard_body��־λ */
    r->count++;
    r->discard_body = 1;

    return NGX_OK;
}

/* �������ngx_http_discard_request_bodyû��һ���Զ�ȡ���а��壬�������ȡ����Ķ����ɸú���ִ�� */
void
ngx_http_discarded_request_body_handler(ngx_http_request_t *r)
{
    ngx_int_t                  rc;
    ngx_msec_t                 timer;
    ngx_event_t               *rev;
    ngx_connection_t          *c;
    ngx_http_core_loc_conf_t  *clcf;

    /* ��ȡ���Ӷ���Ͷ��¼� */
    c = r->connection;
    rev = c->read;

    /*
     * �����־λrev->timedoutΪ1����ʾ��ȡ�����¼���ʱ�����ʱ����Ҫ����ngx_http_finalize_request��������
     * ��ngx_http_discard_request_body���Ѿ������Ӷ��¼��Ӷ�ʱ�����Ƴ��ˣ�������Ϊʲô�ֻ��ж�ʱ����ʱ��?
     * ԭ���������Nginx�������˱�������׼���ر������ʱ��(��ngx_http_finalize_connection())���ֵ�ǰ����
     * ������ִ�ж�������Ķ���(�����ǿͻ��˻�û�н�������巢����)�����ʱ�򻹲���ֱ�ӹر�������Ҫ�ȴ�
     * ��ȡ���������嶯�������������ֲ��������Ƶĵȴ���������Ҫ���������ӳٹرյ�ʱ�䣬�������¼����뵽
     * ��ʱ���У������ʱ����ʱ�����ӳٹرյ�ʱ�䵽�ˣ����ʱ�򽫲��ٵȴ���ֱ�ӹر�����
     */
    if (rev->timedout) {
        c->timedout = 1;
        c->error = 1;
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    /* 
     * r->lingering_timeҲ���������Ѿ������꣬���ǿͻ��˻�û����ȫ���Ͱ�����������
     * ngx_http_finalize_connection()���������õġ���ʱ��Ҫ���Ϊ���տͻ�����������
     * �ӳٹر������ʱ���Ƿ���(������ҵ������Ѿ��������)��������ˣ�Ҳֱ�ӹر�����
     * ��ngx_http_finalize_connection()���Ὣr->lingering_time��ֵΪִ��ngx_http_finalize_connection()
     * �����ĵ�ǰʱ����������ļ������õ��ӳٹر�ʱ�䣬��ʾ����һ�̿�ʼ�������ӳ�clcf->lingering_time
     * ʱ��رգ����ʱ�䵽�ˣ��͹ر�����
     */
    if (r->lingering_time) {
        timer = (ngx_msec_t) r->lingering_time - (ngx_msec_t) ngx_time();

        /* ����ӳٹر������ʱ���Ƿ��� */
        if ((ngx_msec_int_t) timer <= 0) {
            r->discard_body = 0;
            r->lingering_close = 0;
            ngx_http_finalize_request(r, NGX_ERROR);
            return;
        }

    } else {
        /* 
         * r->lingering_timeΪ0������������ҵ����滹û��ִ����ϣ���Ϊr->lingering_timeֻ����
         * ngx_http_finalize_connection�л����ã�ִ��ngx_http_finalize_connection����ʱ�������Ѿ��������� 
         */
        timer = 0;
    }

    /* ��ȡ��Ҫ������������� */
    rc = ngx_http_read_discarded_request_body(r);

    /*
     * ngx_http_read_discarded_request_body����NGX_OK��ʾ�������嶯���ɹ������Թر����ӣ�ͬʱ
     * ����ʾ���ڶ�������ı�־λ���㣬������ִ��ngx_http_finalize_connection()ʱ���ܹر�����
     * ͬʱ���ӳٹرձ�־λ����
     */
    if (rc == NGX_OK) {
        r->discard_body = 0;
        r->lingering_close = 0;
        /*
         * ��NGX_DONEΪ��������ngx_http_finalize_request)()����ngx_http_finalize_request()
         * �����⵽����ΪNGX_DONE��������ngx_http_finalize_connection()���������ü�����1��
         * ������ü���Ϊ0�����ǻ��������ġ�
         */
        ngx_http_finalize_request(r, NGX_DONE);
        return;
    }

    /* ngx_http_read_discarded_request_body����ֵ���ڵ���NGX_HTTP_SPECIAL_RESPONSE��ʾ������������ */
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        c->error = 1;
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    /* rc == NGX_AGAIN */

    /*
     * ngx_http_read_discarded_request_body����NGX_AGAIN��������û�н��յ�������������壬��Ҫ�¼�ģ��
     * �ٴν��е��ȣ��Զ�ȡ������������壬���Խ����Ӷ��¼����뵽epoll��
     */
    if (ngx_handle_read_event(rev, 0) != NGX_OK) {
        c->error = 1;
        ngx_http_finalize_request(r, NGX_ERROR);
        return;
    }

    /*
     * timer��Ϊ0����ʾ������ҵ������Ѿ���������ˣ�ֻ��Ϊ�˽��հ�����ӳٹرգ� ���ʱ����Ҫ�����Ӷ�Ӧ��
     * ���¼����뵽��ʱ���У������ʱ����ʱ�����ٵȴ�����������壬ֱ�ӹر����󣬼���������ͷ����
     */
    if (timer) {

        clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

        timer *= 1000;

        if (timer > clcf->lingering_timeout) {
            timer = clcf->lingering_timeout;
        }

        /*
         * ����ngx_add_timer()�����¼����뵽��ʱ���У�������¼�ԭ�����ڶ�ʱ���У����ɾ��ԭ�еĶ�ʱ����
         * �����¼����¼��뵽��ʱ���У����ʱ���൱�ڶ�ʱʱ���ָ��µ���clcf->lingering_timeout��
         * �������ӳٹر����ʱ���ڣ������ʱ����ʱ��ر���������¼�ģ�鱾�ε��õ��Ȼ���û����ɶ�������
         * ����������Ҫ���¶�ʱ��ʱ������ʾ��һ�ֶ�ʱ��
         */
        ngx_add_timer(rev, timer);
    }
}

/* ���հ��� */
static ngx_int_t
ngx_http_read_discarded_request_body(ngx_http_request_t *r)
{
    size_t     size;
    ssize_t    n;
    ngx_int_t  rc;
    ngx_buf_t  b;
    u_char     buffer[NGX_HTTP_DISCARD_BUFFER_SIZE];

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http read discarded body");

    ngx_memzero(&b, sizeof(ngx_buf_t));

    b.temporary = 1;

    /* ѭ���������Ӷ�Ӧ���ں��׽��ֻ������е��ַ��� */
    for ( ;; ) {
        /*
         * ���ʣ��δ���յİ��峤�ȣ����Ϊ0����ʾ�Ѿ����յ��������İ��壬���ʱ������
         * ���¼��ص�������Ϊngx_http_block_reading����ʾ�������󴥷����¼�ʱ�������κ�
         * ����ͬʱ����NGX_OK�������ϲ��Ѿ��ɹ����������а���
         */
        if (r->headers_in.content_length_n == 0) {
            r->read_event_handler = ngx_http_block_reading;
            return NGX_OK;
        }

        /*
         * r->connection->read->readyΪ0����ʾ���Ӷ�Ӧ���ں��׽��ֻ�����û�пɶ���tcp�ַ�����
         * ����NGX_AGAIN���ȴ��¼�ģ���ٴε���
         */
        if (!r->connection->read->ready) {
            return NGX_AGAIN;
        }

        size = (size_t) ngx_min(r->headers_in.content_length_n,
                                NGX_HTTP_DISCARD_BUFFER_SIZE);

        /* ����Nginx��װ��recv�����ں��׽��ֻ������е��ַ��� */
        n = r->connection->recv(r->connection, buffer, size);

        /* recv����NGX_ERROR��ʾ���ӳ����������еı�־λ������NGX_OK */
        if (n == NGX_ERROR) {
            r->connection->error = 1;
            return NGX_OK;
        }

        /*
         * recv����NGX_AGAIN����ʾ���Ӷ�Ӧ���ں��׽��ֻ�����û�пɶ���tcp�ַ�����
         * ����NGX_AGAIN���ȴ��¼�ģ���ٴε���
         */
        if (n == NGX_AGAIN) {
            return NGX_AGAIN;
        }

        /* ���n == 0��ʾ�ͻ��������ر������ӣ������ٽ��հ����ˣ�����NGX_OK */
        if (n == 0) {
            return NGX_OK;
        }

        b.pos = buffer;
        b.last = buffer + n;

        /* ���������������������Ƿ��Ѿ����յ�������������壬���û�У������ʣ��δ���հ��峤�� */
        rc = ngx_http_discard_request_body_filter(r, &b);

        if (rc != NGX_OK) {
            return rc;
        }
    }
}

/* ���������������������Ƿ��Ѿ����յ�������������壬���û�У������ʣ��δ���հ��峤�� */
static ngx_int_t
ngx_http_discard_request_body_filter(ngx_http_request_t *r, ngx_buf_t *b)
{
    size_t                    size;
    ngx_int_t                 rc;
    ngx_http_request_body_t  *rb;

    if (r->headers_in.chunked) {

        rb = r->request_body;

        if (rb == NULL) {

            rb = ngx_pcalloc(r->pool, sizeof(ngx_http_request_body_t));
            if (rb == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            rb->chunked = ngx_pcalloc(r->pool, sizeof(ngx_http_chunked_t));
            if (rb->chunked == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            r->request_body = rb;
        }

        for ( ;; ) {

            rc = ngx_http_parse_chunked(r, b, rb->chunked);

            if (rc == NGX_OK) {

                /* a chunk has been parsed successfully */

                size = b->last - b->pos;

                if ((off_t) size > rb->chunked->size) {
                    b->pos += (size_t) rb->chunked->size;
                    rb->chunked->size = 0;

                } else {
                    rb->chunked->size -= size;
                    b->pos = b->last;
                }

                continue;
            }

            if (rc == NGX_DONE) {

                /* a whole response has been parsed successfully */

                r->headers_in.content_length_n = 0;
                break;
            }

            if (rc == NGX_AGAIN) {

                /* set amount of data we want to see next time */

                r->headers_in.content_length_n = rb->chunked->length;
                break;
            }

            /* invalid */

            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "client sent invalid chunked body");

            return NGX_HTTP_BAD_REQUEST;
        }

    } else {
        /* ���㻺�������Ѿ����յ��İ��峤�� */
        size = b->last - b->pos;

        /*
         * size > r->headers_in.content_length_n�������������Ѿ����յ�������������ͷ����
         * ���ʱ�򣬽�ָ��ǰ���������ڴ��ָ�����content_length_n���ȣ������������
         * �е�r->headers_in.content_length_n��Ϊ0����ʾ�Ѿ����յ���ȫ�����������
         */
        if ((off_t) size > r->headers_in.content_length_n) {
            b->pos += (size_t) r->headers_in.content_length_n;
            r->headers_in.content_length_n = 0;

        } else {
            /*
             * ����ִ�е��������Ŀǰ��û�н��յ�������������壬��ʱ��b->posָ��ָ��
             * b->last��ʾ�Ѿ���ȡ�������������壬������ʣ��δ���յİ��峤��
             */
            b->pos = b->last;
            r->headers_in.content_length_n -= size;
        }
    }

    return NGX_OK;
}

/* ���Expectͷ������������Ӧ������ͻ��˷��������� */
static ngx_int_t
ngx_http_test_expect(ngx_http_request_t *r)
{
    ngx_int_t   n;
    ngx_str_t  *expect;

    /* �������ͷ���в�û��Expectͷ������http�汾С��http1.1������Ҫ����ͷ������ôֱ�ӷ���NGX_OK */
    if (r->expect_tested
        || r->headers_in.expect == NULL
        || r->http_version < NGX_HTTP_VERSION_11)
    {
        return NGX_OK;
    }

    /* ��expect_tested��־λ��λ����ʾִ�й�Expectͷ����� */
    r->expect_tested = 1;

    expect = &r->headers_in.expect->value;

    /* У��Expectͷ����ֵ */
    if (expect->len != sizeof("100-continue") - 1
        || ngx_strncasecmp(expect->data, (u_char *) "100-continue",
                           sizeof("100-continue") - 1)
           != 0)
    {
        return NGX_OK;
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "send 100 Continue");

    /* ���ͻ��˷���"HTTP/1.1 100 Continue"��Ӧ���ͻ��˽��յ������Ӧ��ʼ���������� */
    n = r->connection->send(r->connection,
                            (u_char *) "HTTP/1.1 100 Continue" CRLF CRLF,
                            sizeof("HTTP/1.1 100 Continue" CRLF CRLF) - 1);

    if (n == sizeof("HTTP/1.1 100 Continue" CRLF CRLF) - 1) {
        return NGX_OK;
    }

    /* we assume that such small packet should be send successfully */

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_request_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    if (r->headers_in.chunked) {
        return ngx_http_request_body_chunked_filter(r, in);

    } else {
        return ngx_http_request_body_length_filter(r, in);
    }
}


static ngx_int_t
ngx_http_request_body_length_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    size_t                     size;
    ngx_int_t                  rc;
    ngx_buf_t                 *b;
    ngx_chain_t               *cl, *tl, *out, **ll;
    ngx_http_request_body_t   *rb;

    rb = r->request_body;

    if (rb->rest == -1) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http request body content length filter");

        rb->rest = r->headers_in.content_length_n;
    }

    out = NULL;
    ll = &out;

    for (cl = in; cl; cl = cl->next) {

        if (rb->rest == 0) {
            break;
        }

        tl = ngx_chain_get_free_buf(r->pool, &rb->free);
        if (tl == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        b = tl->buf;

        ngx_memzero(b, sizeof(ngx_buf_t));

        b->temporary = 1;
        b->tag = (ngx_buf_tag_t) &ngx_http_read_client_request_body;
        b->start = cl->buf->pos;
        b->pos = cl->buf->pos;
        b->last = cl->buf->last;
        b->end = cl->buf->end;
        b->flush = r->request_body_no_buffering;

        size = cl->buf->last - cl->buf->pos;

        if ((off_t) size < rb->rest) {
            cl->buf->pos = cl->buf->last;
            rb->rest -= size;

        } else {
            cl->buf->pos += (size_t) rb->rest;
            rb->rest = 0;
            b->last = cl->buf->pos;
            b->last_buf = 1;
        }

        *ll = tl;
        ll = &tl->next;
    }

    rc = ngx_http_top_request_body_filter(r, out);

    ngx_chain_update_chains(r->pool, &rb->free, &rb->busy, &out,
                            (ngx_buf_tag_t) &ngx_http_read_client_request_body);

    return rc;
}


static ngx_int_t
ngx_http_request_body_chunked_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    size_t                     size;
    ngx_int_t                  rc;
    ngx_buf_t                 *b;
    ngx_chain_t               *cl, *out, *tl, **ll;
    ngx_http_request_body_t   *rb;
    ngx_http_core_loc_conf_t  *clcf;

    rb = r->request_body;

    if (rb->rest == -1) {

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "http request body chunked filter");

        rb->chunked = ngx_pcalloc(r->pool, sizeof(ngx_http_chunked_t));
        if (rb->chunked == NULL) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        r->headers_in.content_length_n = 0;
        rb->rest = 3;
    }

    out = NULL;
    ll = &out;

    for (cl = in; cl; cl = cl->next) {

        for ( ;; ) {

            ngx_log_debug7(NGX_LOG_DEBUG_EVENT, r->connection->log, 0,
                           "http body chunked buf "
                           "t:%d f:%d %p, pos %p, size: %z file: %O, size: %O",
                           cl->buf->temporary, cl->buf->in_file,
                           cl->buf->start, cl->buf->pos,
                           cl->buf->last - cl->buf->pos,
                           cl->buf->file_pos,
                           cl->buf->file_last - cl->buf->file_pos);

            rc = ngx_http_parse_chunked(r, cl->buf, rb->chunked);

            if (rc == NGX_OK) {

                /* a chunk has been parsed successfully */

                clcf = ngx_http_get_module_loc_conf(r, ngx_http_core_module);

                if (clcf->client_max_body_size
                    && clcf->client_max_body_size
                       - r->headers_in.content_length_n < rb->chunked->size)
                {
                    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                                  "client intended to send too large chunked "
                                  "body: %O+%O bytes",
                                  r->headers_in.content_length_n,
                                  rb->chunked->size);

                    r->lingering_close = 1;

                    return NGX_HTTP_REQUEST_ENTITY_TOO_LARGE;
                }

                tl = ngx_chain_get_free_buf(r->pool, &rb->free);
                if (tl == NULL) {
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }

                b = tl->buf;

                ngx_memzero(b, sizeof(ngx_buf_t));

                b->temporary = 1;
                b->tag = (ngx_buf_tag_t) &ngx_http_read_client_request_body;
                b->start = cl->buf->pos;
                b->pos = cl->buf->pos;
                b->last = cl->buf->last;
                b->end = cl->buf->end;
                b->flush = r->request_body_no_buffering;

                *ll = tl;
                ll = &tl->next;

                size = cl->buf->last - cl->buf->pos;

                if ((off_t) size > rb->chunked->size) {
                    cl->buf->pos += (size_t) rb->chunked->size;
                    r->headers_in.content_length_n += rb->chunked->size;
                    rb->chunked->size = 0;

                } else {
                    rb->chunked->size -= size;
                    r->headers_in.content_length_n += size;
                    cl->buf->pos = cl->buf->last;
                }

                b->last = cl->buf->pos;

                continue;
            }

            if (rc == NGX_DONE) {

                /* a whole response has been parsed successfully */

                rb->rest = 0;

                tl = ngx_chain_get_free_buf(r->pool, &rb->free);
                if (tl == NULL) {
                    return NGX_HTTP_INTERNAL_SERVER_ERROR;
                }

                b = tl->buf;

                ngx_memzero(b, sizeof(ngx_buf_t));

                b->last_buf = 1;

                *ll = tl;
                ll = &tl->next;

                break;
            }

            if (rc == NGX_AGAIN) {

                /* set rb->rest, amount of data we want to see next time */

                rb->rest = rb->chunked->length;

                break;
            }

            /* invalid */

            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                          "client sent invalid chunked body");

            return NGX_HTTP_BAD_REQUEST;
        }
    }

    rc = ngx_http_top_request_body_filter(r, out);

    ngx_chain_update_chains(r->pool, &rb->free, &rb->busy, &out,
                            (ngx_buf_tag_t) &ngx_http_read_client_request_body);

    return rc;
}


ngx_int_t
ngx_http_request_body_save_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_buf_t                 *b;
    ngx_chain_t               *cl;
    ngx_http_request_body_t   *rb;

    rb = r->request_body;

#if (NGX_DEBUG)

    for (cl = rb->bufs; cl; cl = cl->next) {
        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, r->connection->log, 0,
                       "http body old buf t:%d f:%d %p, pos %p, size: %z "
                       "file: %O, size: %O",
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);
    }

    for (cl = in; cl; cl = cl->next) {
        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, r->connection->log, 0,
                       "http body new buf t:%d f:%d %p, pos %p, size: %z "
                       "file: %O, size: %O",
                       cl->buf->temporary, cl->buf->in_file,
                       cl->buf->start, cl->buf->pos,
                       cl->buf->last - cl->buf->pos,
                       cl->buf->file_pos,
                       cl->buf->file_last - cl->buf->file_pos);
    }

#endif

    /* TODO: coalesce neighbouring buffers */

    if (ngx_chain_add_copy(r->pool, &rb->bufs, in) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    if (r->request_body_no_buffering) {
        return NGX_OK;
    }

    if (rb->rest > 0) {

        if (rb->buf && rb->buf->last == rb->buf->end
            && ngx_http_write_request_body(r) != NGX_OK)
        {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        return NGX_OK;
    }

    /* rb->rest == 0 */

    if (rb->temp_file || r->request_body_in_file_only) {

        if (ngx_http_write_request_body(r) != NGX_OK) {
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }

        if (rb->temp_file->file.offset != 0) {

            cl = ngx_chain_get_free_buf(r->pool, &rb->free);
            if (cl == NULL) {
                return NGX_HTTP_INTERNAL_SERVER_ERROR;
            }

            b = cl->buf;

            ngx_memzero(b, sizeof(ngx_buf_t));

            b->in_file = 1;
            b->file_last = rb->temp_file->file.offset;
            b->file = &rb->temp_file->file;

            rb->bufs = cl;
        }
    }

    return NGX_OK;
}

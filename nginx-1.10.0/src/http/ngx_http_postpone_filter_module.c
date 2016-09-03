
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

/*
 *     Nginx�Ƕ���̵Ĵ���ܹ������Զ��ڵ������̶����ǲ��ܹ����������ģ����һ����������ĳ��ԭ�������˵�ǰ
 * ���̣�����ζ�Ÿý��̾Ͳ��ܴ���ý���֮ǰ���յ������������ˣ�Ҳ���ܼ��������ŵ������ˡ������ʵ��Ӧ��
 * ������ĳ���������ڴ�����Ҫ���������̣���ôNginx�����������ø������һЩ״̬������������뵽epoll��
 * ���м�أ��ȴ��¼�ģ������ٽ���������Ȼ��תȥ�������������ˡ�������ζ��һ�����������Ҫִ�ж��
 * ������ɴ�������һ������Ķ����������ԣ���ζ��������ɵ��Ⱥ�˳������Ǳ�������˳��һ����һ�µģ�
 * ������֪�����͵��ͻ��˵�����һ��Ҫ���������󴴽�˳�������ͣ�������Ҫ��һ�ֻ�������֤�����ĳ��������
 * ��ǰ��ɵ���������Ҫ�еط������������ݶ�����ֱ�������out chain�У�ͬʱҲҪ�ܹ��ÿ������ͻ��˷���
 * ���ݵ������������������������������������ݡ����ֻ�����ͨ�����Ӷ���ngx_connection_t�е�data�ֶΡ�
 * ����ģ��ngx_http_postpone_filter_module��ngx_http_finalize_request�еĲ����߼���ͬʵ�ֵġ�
 *     �������ݲο�: http://blog.csdn.net/fengmo_q/article/details/6685840
 *     ��ngx_http_subrequest()�������ᵽ���Ӷ����е�data�ֶ�ָ����ǵ�ǰ�������ͻ��˷�����Ӧ������
 */


static ngx_int_t ngx_http_postpone_filter_add(ngx_http_request_t *r,
    ngx_chain_t *in);
static ngx_int_t ngx_http_postpone_filter_init(ngx_conf_t *cf);


static ngx_http_module_t  ngx_http_postpone_filter_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_postpone_filter_init,         /* postconfiguration */

    NULL,                                  /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    NULL,                                  /* create location configuration */
    NULL                                   /* merge location configuration */
};


ngx_module_t  ngx_http_postpone_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_postpone_filter_module_ctx,  /* module context */
    NULL,                                  /* module directives */
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

/* �������й���ģ�������ָ�� */
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;


static ngx_int_t
ngx_http_postpone_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_connection_t              *c;
    ngx_http_postponed_request_t  *pr;

    c = r->connection;

    ngx_log_debug3(NGX_LOG_DEBUG_HTTP, c->log, 0,
                   "http postpone filter \"%V?%V\" %p", &r->uri, &r->args, in);

    /*
     * r != c->data������ǰ��������out chain�з�����Ӧ����(����������ǰ���)����ʱ��Ҫ��in�е���Ӧ����
     * �������Լ���postponed�����У���Ϊ������ȿ�������������������������Ҳ��������
     * ��������������Ӧ���ݡ�
     */
    if (r != c->data) {

        if (in) {
            ngx_http_postpone_filter_add(r, in);
            return NGX_OK;
        }

#if 0
        /* TODO: SSI may pass NULL */
        ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                      "http postpone filter NULL inactive request");
#endif

        return NGX_OK;
    }

    /*
     * ����ִ�е����������ǰ���������out chain�з������ݣ������ǰ����û��������ڵ㣬Ҳû�����ݽڵ㣬
     * ��ֱ�ӷ��͵ĵ�ǰ��Ӧ����in���߼��������ϴ�û�з������out�����е���Ӧ����
     */
    if (r->postponed == NULL) {

        if (in || c->buffered) {
            return ngx_http_next_body_filter(r->main, in);
        }

        return NGX_OK;
    }

    /*
     * ����ִ�е����������Ȼ��ǰ���������out chain�з������ݣ����Ǹ�������������������ݽڵ㣬�����������
     * ��Ҫ�ȴ���������������ݽڵ㣬��ô���ʱ����Ҫ�Ƚ��˴�Ҫ���͵����ݱ������Լ���postponed������
     */
    if (in) {
        ngx_http_postpone_filter_add(r, in);
    }

    /*
     * ����ǰ�����postponed�����п�����������ڵ㣬Ҳ�п��������ݽڵ�
     */
    do {
        pr = r->postponed;

        /*
         * �����ǰr->postponed����ڵ�洢��������������������ص�ԭʼ�����posted_requests������
         * �������Ա�֤�´�ִ��ngx_http_run_posted_requests()�ǿ��Դ������������
         */
        if (pr->request) {

            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,
                           "http postpone filter wake \"%V?%V\"",
                           &pr->request->uri, &pr->request->args);

            r->postponed = pr->next;  // ��r->postponedָ����һ���ڵ�

            /* ��Ϊ�������������out chain�з������ݵ����ȼ������������Խ�c->date��Ϊ������ */
            c->data = pr->request;

            /* �������������뵽ԭʼ�����posted_requests����ĩ�� */
            return ngx_http_post_request(pr->request, NULL);
        }

        /*
         * ����ִ�е����������ǰ�����posted�����еĽڵ������һ�����ݽڵ�
         */
         
        if (pr->out == NULL) {
            ngx_log_error(NGX_LOG_ALERT, c->log, 0,
                          "http postpone filter NULL output");

        } else {
            ngx_log_debug2(NGX_LOG_DEBUG_HTTP, c->log, 0,
                           "http postpone filter output \"%V?%V\"",
                           &r->uri, &r->args);

            /* ����������ݽڵ��е����ݷ��͵�out chain������ */
            if (ngx_http_next_body_filter(r->main, pr->out) == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

        r->postponed = pr->next;  // �����������������r->postponed�����е���һ���ڵ�

    } while (r->postponed);

    return NGX_OK;
}

/*
 * ������Ĳ���������in���ص������postponed����ĩβ�ڵ��out��Ա��
 */
static ngx_int_t
ngx_http_postpone_filter_add(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_http_postponed_request_t  *pr, **ppr;

    /*
     * ������ǰ�����postponed��Ա������ĩβ��Ȼ����ĩβ����һ��ngx_http_postponed_request_t����
     * �Ľڵ㣬Ȼ��in�����е����ݴ�ŵ�ngx_http_postponed_request_t���ͽڵ��out�ֶ���
     */
    if (r->postponed) {
        for (pr = r->postponed; pr->next; pr = pr->next) { /* void */ }

        if (pr->request == NULL) {
            goto found;
        }

        ppr = &pr->next;

    } else {
        ppr = &r->postponed;
    }

    /*
     * �������ڴ�����ݵ�ngx_http_postponed_request_t����
     */
    pr = ngx_palloc(r->pool, sizeof(ngx_http_postponed_request_t));
    if (pr == NULL) {
        return NGX_ERROR;
    }

    /* ���������뵽��ngx_http_postponed_request_t������ڵ������postponed����ĩβ */
    *ppr = pr;

    pr->request = NULL;
    pr->out = NULL;
    pr->next = NULL;

found:

    /* ��in�е��������ݻ��������ص�chain�� */
    if (ngx_chain_add_copy(r->pool, &pr->out, in) == NGX_OK) {
        return NGX_OK;
    }

    return NGX_ERROR;
}


static ngx_int_t
ngx_http_postpone_filter_init(ngx_conf_t *cf)
{
    ngx_http_next_body_filter = ngx_http_top_body_filter;
    ngx_http_top_body_filter = ngx_http_postpone_filter;

    return NGX_OK;
}

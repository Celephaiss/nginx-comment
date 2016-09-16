
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_EVENT_PIPE_H_INCLUDED_
#define _NGX_EVENT_PIPE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


typedef struct ngx_event_pipe_s  ngx_event_pipe_t;

typedef ngx_int_t (*ngx_event_pipe_input_filter_pt)(ngx_event_pipe_t *p,
                                                    ngx_buf_t *buf);
typedef ngx_int_t (*ngx_event_pipe_output_filter_pt)(void *data,
                                                     ngx_chain_t *chain);


struct ngx_event_pipe_s {
    ngx_connection_t  *upstream;  // Nginx�����η�����֮������Ӷ���
    ngx_connection_t  *downstream;  // Nginx�����οͻ���֮������Ӷ���

    /*
     * ֱ�ӽ������η�������Ӧ�Ļ���������(δ����ʵ���ڴ�)����ʾһ��ngx_event_pipe_read_upstream����
     * ���ù����н��յ�����Ӧ
     */
    ngx_chain_t       *free_raw_bufs;

    /*
     * ��ʾ���յ������η���������Ӧ����������input_filter�����лὫfree_raw_bufs�еĻ��������ص�in��
     * in�����еĻ�����ָ������ڴ�
     */
    ngx_chain_t       *in;

    /* ָ��ոս��յ��Ļ����� */
    ngx_chain_t      **last_in;

    ngx_chain_t       *writing;

    /*
     * �����Ž�Ҫ���͸��ͻ��˵Ļ����������ڽ�in�����е�����д����ʱ�ļ�ʱ�ͻὫ������д���
     * ���������ص�out�У�out�����еĻ�������Ӧ��Ҳ��ָ�����ļ���
     */
    ngx_chain_t       *out;

    /* ���п��õĵĻ��������� */
    ngx_chain_t       *free;

    /*
     * ָ���ϴη��͸��ͻ���ʱû�з�����Ļ�����������������еĻ������Ѿ����浽��
     * ��������out�����У�busy�����ڼ�¼���ж�����Ӧ�ȴ�����
     */
    ngx_chain_t       *busy;

    /*
     * the input filter i.e. that moves HTTP/1.1 chunks
     * from the raw bufs to an incoming chain
     */
    /* ������յ����������η������Ļ����� */
    ngx_event_pipe_input_filter_pt    input_filter;
    
    /* ���ݸ�input_filter�����Ĳ�����һ�������Ϊngx_http_request_t���� */
    void                             *input_ctx;

    /* ������Ӧ���ͻ��˵ķ��� */
    ngx_event_pipe_output_filter_pt   output_filter;

    /* ���ݸ�output_filter�����Ĳ�����һ������Ϊngx_http_request_t���� */
    void                             *output_ctx;

#if (NGX_THREADS)
    ngx_int_t                       (*thread_handler)(ngx_thread_task_t *task,
                                                      ngx_file_t *file);
    void                             *thread_ctx;
    ngx_thread_task_t                *thread_task;
#endif

    unsigned           read:1;  // �Ƿ��ȡ�������η�������Ӧ�ı�־λ
    unsigned           cacheable:1;  // �Ƿ����ļ������־λ
    unsigned           single_buf:1;  // Ϊ1��ʾ����������Ӧʱһ��ֻ�ܽ���һ��ngx_buf_t������
    unsigned           free_bufs:1;  // Ϊ1��ʾһ��������������Ӧ���壬�������������ͷŻ�����
    unsigned           upstream_done:1;  // ��ʾNginx�����η�������������
    unsigned           upstream_error:1;  // ��ʾNginx�����η�����֮������ӳ���
    unsigned           upstream_eof:1;  // ��ʾNginx�����η�����������״̬��Ϊ1��ʾ�����Ѿ��ر�
    /*
     * ��ʾ��ʱ������ȡ������Ӧ�����̣��ڴ�ͨ��������Ӧ�����οͻ�������������еĻ�����������
     * ���еĻ�����������Ӧ����upstream_blockedΪ1ʱ����ngx_event_pipe������ѭ���л��ȵ���
     * ngx_event_pipe_write_to_downstream������Ӧ���ٵ���ngx_event_pipe_read_upstream��������ȡ
     * ������Ӧ��
     */
    unsigned           upstream_blocked:1;
    unsigned           downstream_done:1;  // Nginx�����οͻ��˽���������־λ
    unsigned           downstream_error:1;  // Nginx�����οͻ������ӳ����־λ
    unsigned           cyclic_temp_file:1;  // Ϊ1��ʾ��ͼ������ʱ�ļ�������ʹ�ù��Ŀռ�
    unsigned           aio:1;  // ���ڽ����첽io�ı�־

    /* �Ѿ�����Ļ�������Ŀ����bufs.num��Ա������ */
    ngx_int_t          allocated;

    /* 
     * bufs��¼�����ڽ������η�������Ӧ���ڴ滺������С��
     * ����bufs.size��ʾÿ����������С��bufs.num��ʾ��������Ŀ
     */
    ngx_bufs_t         bufs;
    ngx_buf_tag_t      tag;

    /* 
     * ����busy�������д�������Ӧ���ȵĴ���ֵ�����ﵽbusy_sizeʱ������ȴ�
     * busy�������������㹻�����ݣ����ܼ�������out��in�������е�����
     */
    ssize_t            busy_size;

    off_t              read_length;  // �Ѿ����յ���������Ӧ�ĳ���
    off_t              length;  // ʣ��δ���յ�������Ӧ�ĳ���

    /* ����������Ӧ����ʱ�ļ�����󳤶� */
    off_t              max_temp_file_size;

    /* һ�ο�������ʱ�ļ�д�����ݵ���󳤶� */
    ssize_t            temp_file_write_size;

    /* ��ȡ������Ӧ�ĳ�ʱʱ�� */
    ngx_msec_t         read_timeout;

    /* ������Ӧ�����εĳ�ʱʱ�� */
    ngx_msec_t         send_timeout;

    /* ������Ӧ�����ε�tcp���ӵĻ�����"ˮλ��" */
    ssize_t            send_lowat;

    ngx_pool_t        *pool;
    ngx_log_t         *log;

    /* Ԥ���ջ�������ָ���ڽ�����Ӧ��ͷʱ���յĲ�����Ӧ�������� */
    ngx_chain_t       *preread_bufs;

    /* Ԥ���յ���Ӧ�������� */
    size_t             preread_size;
    ngx_buf_t         *buf_to_file;  // �����ļ����泡��

    size_t             limit_rate;  // ��������
    time_t             start_sec;  // ��ʼ������Ӧ��ʱ���

    /* �������η�������Ӧ����ʱ�ļ� */
    ngx_temp_file_t   *temp_file;

    /* STUB */ int     num;
};


ngx_int_t ngx_event_pipe(ngx_event_pipe_t *p, ngx_int_t do_write);
ngx_int_t ngx_event_pipe_copy_input_filter(ngx_event_pipe_t *p, ngx_buf_t *buf);
ngx_int_t ngx_event_pipe_add_free_buf(ngx_event_pipe_t *p, ngx_buf_t *b);


#endif /* _NGX_EVENT_PIPE_H_INCLUDED_ */

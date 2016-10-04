
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CONNECTION_H_INCLUDED_
#define _NGX_CONNECTION_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_listening_s  ngx_listening_t;

struct ngx_listening_s {
    ngx_socket_t        fd;  //socket�׽��־��

    struct sockaddr    *sockaddr;  //������sockaddr��ַ
    socklen_t           socklen;    /* size of sockaddr */
    size_t              addr_text_max_len;  //�ַ�����ʽ��ip��ַ����󳤶ȣ�ָ����addr_text���ڴ��С
    ngx_str_t           addr_text;  //�洢�ַ�����ʽ��ip��ַ

    int                 type;  //�׽������ͣ���SOCK_STREAM��ʾtcp

    /*tcpʵ�ּ���ʱ��backlog���У�����ʾ��������ͨ���������ֽ���tcp���ӵ���û���κν��̿�ʼ���������������*/
    int                 backlog;
    /*�ں��ж�������׽��ԵĽ��ܻ������Ĵ�С ���ͻ������Ĵ�С*/
    int                 rcvbuf;
    int                 sndbuf;
#if (NGX_HAVE_KEEPALIVE_TUNABLE)
    int                 keepidle;
    int                 keepintvl;
    int                 keepcnt;
#endif

    /* handler of accepted connection */
    ngx_connection_handler_pt   handler;  //�µ�tcp���ӳɹ������˺�Ĵ�����

    /*���ڱ��浱ǰ�����˿ڶ�Ӧ�ŵ����м�����ַ��Ϣ��ÿ��������ַ(ip:port)�����ż��������ַ������server��Ϣ*/
    void               *servers;  /* array of ngx_http_in_addr_t, for example */

    /*��־����*/
    ngx_log_t           log;
    ngx_log_t          *logp;

    size_t              pool_size;  //�µ�tcp���ӵ��ڴ�ش�С
    /* should be here because of the AcceptEx() preread */
    size_t              post_accept_buffer_size;
    /* should be here because of the deferred accept */
    ngx_msec_t          post_accept_timeout;

    ngx_listening_t    *previous;  //�洢�����ϰ汾Nginx�к��°汾Nginx����ͬһ����ַ�ļ�������
    ngx_connection_t   *connection;  //��ǰ�����˿ڶ�Ӧ�ŵ�����

    ngx_uint_t          worker;  // worker�ӽ��̵ı��

    /*Ϊ1ʱ��ʾ��ǰ���������Ч����ִ��ngx_init_cycleʱ���رռ����˿�*/
    unsigned            open:1;
    /*
     * Ϊ1ʱ��ʾʹ�����е�ngx_cycle_t�ṹ������ʼ���µ�ngx_cycle_t�ṹ��ʱ�����ر�ԭ�ȴ򿪵ļ����˿�;
     * Ϊ0ʱ��ʾ�����ر������򿪵ļ����˿�
     */
    unsigned            remain:1;
    /*
     * Ϊ1ʱ��ʾ�������õ�ǰ��ngx_listening_t�ṹ����׽��֣�Ϊ0ʱ������ʼ���׽���
     */
    unsigned            ignore:1;

    unsigned            bound:1;       /* already bound */
    /*��ʾ��ǰ��������Ƿ�����ǰһ�����̣����Ϊ1�����ʾ����ǰһ�����̣�һ��ᱣ��֮ǰ�Ѿ����úõ��׽��֣������ı�*/
    unsigned            inherited:1;   /* inherited from previous process */
    unsigned            nonblocking_accept:1;
    /*Ϊ1ʱ��ʾ��ǰ�ṹ���Ӧ���׽����Ѿ�����*/
    unsigned            listen:1;
    /*��ʾ�׽����Ƿ�����*/
    unsigned            nonblocking:1;
    unsigned            shared:1;    /* shared between threads or processes */
    /*Ϊ1ʱ��ʾNginx�Ὣ�����ַת��Ϊ�ַ�����ʽ�ĵ�ַ*/
    unsigned            addr_ntop:1;
    unsigned            wildcard:1;

#if (NGX_HAVE_INET6 && defined IPV6_V6ONLY)
    unsigned            ipv6only:1;
#endif
#if (NGX_HAVE_REUSEPORT)
    unsigned            reuseport:1;
    unsigned            add_reuseport:1;
#endif
    unsigned            keepalive:2;

#if (NGX_HAVE_DEFERRED_ACCEPT)
    unsigned            deferred_accept:1;
    unsigned            delete_deferred:1;
    unsigned            add_deferred:1;
#ifdef SO_ACCEPTFILTER
    char               *accept_filter;
#endif
#endif
#if (NGX_HAVE_SETFIB)
    int                 setfib;
#endif

#if (NGX_HAVE_TCP_FASTOPEN)
    int                 fastopen;
#endif

};


typedef enum {
    NGX_ERROR_ALERT = 0,
    NGX_ERROR_ERR,
    NGX_ERROR_INFO,
    NGX_ERROR_IGNORE_ECONNRESET,
    NGX_ERROR_IGNORE_EINVAL
} ngx_connection_log_error_e;


typedef enum {
    NGX_TCP_NODELAY_UNSET = 0,
    NGX_TCP_NODELAY_SET,
    NGX_TCP_NODELAY_DISABLED
} ngx_connection_tcp_nodelay_e;


typedef enum {
    NGX_TCP_NOPUSH_UNSET = 0,
    NGX_TCP_NOPUSH_SET,
    NGX_TCP_NOPUSH_DISABLED
} ngx_connection_tcp_nopush_e;


#define NGX_LOWLEVEL_BUFFERED  0x0f
#define NGX_SSL_BUFFERED       0x01
#define NGX_HTTP_V2_BUFFERED   0x02


struct ngx_connection_s {
    /*
     * ����δʹ��ʱ��data��Ա���ڳ䵱���ӳ��п������������nextָ�룬�����ӱ�ʹ��ʱ��data��Ա�ɵ�������ģ�鶨��
     */
    void               *data;
    ngx_event_t        *read;  //���Ӷ�Ӧ�Ķ��¼�
    ngx_event_t        *write; //���Ӷ�Ӧ��д�¼�

    ngx_socket_t        fd;  //�����Ӷ�Ӧ���׽��־��

    ngx_recv_pt         recv;  //ֱ�ӽ��������ֽ����ķ���
    ngx_send_pt         send;  //ֱ�ӷ��������ֽ����ķ���
    ngx_recv_chain_pt   recv_chain;  //��ngx_chain_t����Ϊ���������������ֽ����ķ���
    ngx_send_chain_pt   send_chain;  //��ngx_chain_t����Ϊ���������������ֽ����ķ���

    ngx_listening_t    *listening; //������Ӷ�Ӧ��ngx_listening_t���󣬴�������listening�����˿ڵ��¼�����

    off_t               sent;  //�������Ѿ����ͳ�ȥ���ֽ���

    ngx_log_t          *log;

    ngx_pool_t         *pool; //����ڴ�صĴ�С��listening���������е�pool_size��Ա����

    int                 type;  //��������

    struct sockaddr    *sockaddr;  //�ͻ��˵�sockaddr�ṹ��
    socklen_t           socklen;  //sockaddr�ṹ�峤��
    ngx_str_t           addr_text;  //�ַ�����ʽ�Ŀͻ���ip��ַ

    ngx_str_t           proxy_protocol_addr;

#if (NGX_SSL)
    ngx_ssl_connection_t  *ssl;
#endif

    /*���������˿ڶ�Ӧ��sockaddr�ṹ�壬Ҳ����listening�����е�sockaddr��Ա*/
    struct sockaddr    *local_sockaddr;
    socklen_t           local_socklen;

    /*���ڽ��ա�����ͻ��˷������ֽ�����ÿ���¼�����ģ��������о��������ӳ��з�����Ŀռ��buffer*/
    ngx_buf_t          *buffer;

    /*��������ǰ������˫���������ʽ��ӵ�ngx_cycle_t�ṹ���reusable_connections_queue˫�������У���ʾ����������*/
    ngx_queue_t         queue;

    /*
     * ����ʹ�õĴ�����ngx_connection_t�ṹ��ÿ�ν���һ�����Կͻ��˵�����,���������������˷�������������ʱ��
     * number�����1.
     */
    ngx_atomic_uint_t   number;

    ngx_uint_t          requests;  //������������

    /*�����е�ҵ������*/
    unsigned            buffered:8;

    unsigned            log_error:3;  //��־����    /* ngx_connection_log_error_e */

    unsigned            unexpected_eof:1;
    unsigned            timedout:1;  //Ϊ1��ʾ�����Ѿ���ʱ
    unsigned            error:1;  //Ϊ1��ʾ���Ӵ�������г���
    unsigned            destroyed:1;

    unsigned            idle:1;  //Ϊ1��ʾ���ڿ���״̬����keepalive��������������֮���״̬
    unsigned            reusable:1;  //Ϊ1��ʾ�����ã���ʾ���Ա��ͷŹ�������ʹ�ã���������queue�ֶ����ʹ��
    unsigned            close:1;  //��ʾ���ӹر�
    unsigned            shared:1;

    unsigned            sendfile:1;  //Ϊ1ʱ��ʾ�����ļ��е����ݷ�����һ��
    
    /*
     * Ϊ1ʱ��ʾֻ�е������׽��ֶ�Ӧ�ķ��ͻ�������������������õĴ�С��ֵ�ǣ��¼�����ģ��Ż�ַ����¼�
     */
    unsigned            sndlowat:1;

    /*tcp���ӵ�nodelay��nopush����*/
    unsigned            tcp_nodelay:2;   /* ngx_connection_tcp_nodelay_e */
    unsigned            tcp_nopush:2;    /* ngx_connection_tcp_nopush_e */

    unsigned            need_last_buf:1;

#if (NGX_HAVE_IOCP)
    unsigned            accept_context_updated:1;
#endif

#if (NGX_HAVE_AIO_SENDFILE)
    unsigned            busy_count:2;
#endif

#if (NGX_THREADS)
    ngx_thread_task_t  *sendfile_task;
#endif
};


#define ngx_set_connection_log(c, l)                                         \
                                                                             \
    c->log->file = l->file;                                                  \
    c->log->next = l->next;                                                  \
    c->log->writer = l->writer;                                              \
    c->log->wdata = l->wdata;                                                \
    if (!(c->log->log_level & NGX_LOG_DEBUG_CONNECTION)) {                   \
        c->log->log_level = l->log_level;                                    \
    }


ngx_listening_t *ngx_create_listening(ngx_conf_t *cf, void *sockaddr,
    socklen_t socklen);
ngx_int_t ngx_clone_listening(ngx_conf_t *cf, ngx_listening_t *ls);
ngx_int_t ngx_set_inherited_sockets(ngx_cycle_t *cycle);
ngx_int_t ngx_open_listening_sockets(ngx_cycle_t *cycle);
void ngx_configure_listening_sockets(ngx_cycle_t *cycle);
void ngx_close_listening_sockets(ngx_cycle_t *cycle);
void ngx_close_connection(ngx_connection_t *c);
void ngx_close_idle_connections(ngx_cycle_t *cycle);
ngx_int_t ngx_connection_local_sockaddr(ngx_connection_t *c, ngx_str_t *s,
    ngx_uint_t port);
ngx_int_t ngx_connection_error(ngx_connection_t *c, ngx_err_t err, char *text);

ngx_connection_t *ngx_get_connection(ngx_socket_t s, ngx_log_t *log);
void ngx_free_connection(ngx_connection_t *c);

void ngx_reusable_connection(ngx_connection_t *c, ngx_uint_t reusable);

#endif /* _NGX_CONNECTION_H_INCLUDED_ */

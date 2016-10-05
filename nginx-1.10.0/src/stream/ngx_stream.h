
/*
 * Copyright (C) Roman Arutyunyan
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_STREAM_H_INCLUDED_
#define _NGX_STREAM_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>

#if (NGX_STREAM_SSL)
#include <ngx_stream_ssl_module.h>
#endif


typedef struct ngx_stream_session_s  ngx_stream_session_t;


#include <ngx_stream_upstream.h>
#include <ngx_stream_upstream_round_robin.h>

/* stream{}���´洢����streamģ���������ṹ��������� */
typedef struct {
    void                  **main_conf;  // �洢����streamģ�����ɵ�main����������ṹ��ָ������
    void                  **srv_conf;  // �洢����streamģ�����ɵ�srv����������ṹ���ָ������
} ngx_stream_conf_ctx_t;

/* �洢listen����ָ��Ĳ��� */
typedef struct {
    union {
        struct sockaddr     sockaddr;
        struct sockaddr_in  sockaddr_in;
#if (NGX_HAVE_INET6)
        struct sockaddr_in6 sockaddr_in6;
#endif
#if (NGX_HAVE_UNIX_DOMAIN)
        struct sockaddr_un  sockaddr_un;
#endif
        u_char              sockaddr_data[NGX_SOCKADDRLEN];
    } u;  // listenָ�����socket��ַ��Ϣ

    socklen_t               socklen;

    /* server ctx */
    ngx_stream_conf_ctx_t  *ctx;  // �洢����server��ʱ���ɵ�������������

    unsigned                bind:1;  // bind�����Ƿ����õı�־λ
    unsigned                wildcard:1;  // listenָ�������ip:port�Ƿ����ͨ����ı�־λ
#if (NGX_STREAM_SSL)
    unsigned                ssl:1;
#endif
#if (NGX_HAVE_INET6 && defined IPV6_V6ONLY)
    unsigned                ipv6only:1;
#endif
#if (NGX_HAVE_REUSEPORT)
    unsigned                reuseport:1;
#endif
    unsigned                so_keepalive:2;
#if (NGX_HAVE_KEEPALIVE_TUNABLE)
    int                     tcp_keepidle;
    int                     tcp_keepintvl;
    int                     tcp_keepcnt;
#endif
    int                     backlog;  // �����׽ӿڵĶ��г���
    int                     type;  // listen������socket����
} ngx_stream_listen_t;


typedef struct {
    ngx_stream_conf_ctx_t  *ctx;  // �洢����server��ʱ���ɵ�������������
    ngx_str_t               addr_text;
#if (NGX_STREAM_SSL)
    ngx_uint_t              ssl;    /* unsigned   ssl:1; */
#endif
} ngx_stream_addr_conf_t;

typedef struct {
    in_addr_t               addr;
    ngx_stream_addr_conf_t  conf;  // �õ�ַ��Ϣ��Ӧ��server��������Ϣ�����ں���Ѱ�ҵ�server��
} ngx_stream_in_addr_t;


#if (NGX_HAVE_INET6)

typedef struct {
    struct in6_addr         addr6;
    ngx_stream_addr_conf_t  conf;
} ngx_stream_in6_addr_t;

#endif

/* �ýṹ���ڷ������й�����ʹ�� */
typedef struct {
    /* ngx_stream_in_addr_t or ngx_stream_in6_addr_t */
    void                   *addrs;  // ��ŵ�ַ��ָ������
    ngx_uint_t              naddrs;  // ָ������ĳ���
} ngx_stream_port_t;

/* �ýṹ�����ڽ��������ļ���ʱ��ʹ�� */
typedef struct {
    int                     family;  // Э����
    int                     type;  // socket����
    in_port_t               port;  // �˿ں�
    /* ��ż����ö˿ڵ����е�ַ��Ϣ */
    ngx_array_t             addrs;       /* array of ngx_stream_conf_addr_t */
} ngx_stream_conf_port_t;


typedef struct {
    ngx_stream_listen_t     opt;  // ip��ַ��Ӧ��listen�������Ϣ
} ngx_stream_conf_addr_t;


typedef ngx_int_t (*ngx_stream_access_pt)(ngx_stream_session_t *s);

/* ngx_stream_core_moduleģ��main�����������ṹ�� */
typedef struct {
    /*
     * servers��̬�����ŵ��Ǵ��������stream���ڵ�server���������ṹ�壬
     * ��ngx_stream_core_server�����лὫ���ɵ�ngx_stream_core_moduleģ���srv����
     * ������ṹ����ӵ������̬�����С�
     */
    ngx_array_t             servers;     /* ngx_stream_core_srv_conf_t */

    /* ��ŵ���stream��������server���ڳ��ֵ�listenָ��Ĳ�����һ��listen��Ӧ���е�һ��Ԫ�� */
    ngx_array_t             listen;      /* ngx_stream_listen_t */
    ngx_stream_access_pt    limit_conn_handler;  // stream limit connģ��ע��Ĵ�����
    ngx_stream_access_pt    access_handler;  // stream accessģ��ע��Ĵ�����
} ngx_stream_core_main_conf_t;


typedef void (*ngx_stream_handler_pt)(ngx_stream_session_t *s);

/* ngx_stream_core_moduleģ��srv�����������ṹ�� */
typedef struct {
    ngx_stream_handler_pt   handler;  // �������ģ����к�������
    ngx_stream_conf_ctx_t  *ctx;  // �洢����server��ʱ���ɵ�������������
    u_char                 *file_name;  // ָ�������ļ�������
    ngx_int_t               line;
    ngx_log_t              *error_log;  // �洢error_logָ��Ĳ���
    ngx_flag_t              tcp_nodelay;  // �洢tcp_nodelayָ��Ĳ���
} ngx_stream_core_srv_conf_t;


struct ngx_stream_session_s {
    uint32_t                signature;         /* "STRM" */

    ngx_connection_t       *connection;  // �ͻ�����Nginx֮������Ӷ���

    off_t                   received;  // �ѽ��յ����Կͻ��˽ӵ����ݳ���

    ngx_log_handler_pt      log_handler;

    void                  **ctx;  // ģ��������
    void                  **main_conf;  // stream��������streamģ�����ɵ�������ṹ������
    void                  **srv_conf;  // ����ƥ���server��������streamģ�����ɵ�������ṹ������

    ngx_stream_upstream_t  *upstream;  // upstream����
};


typedef struct {
    /* ������stream���ڵ�����������֮��ص� */
    ngx_int_t             (*postconfiguration)(ngx_conf_t *cf);

    /*
     * �������ڴ洢streamģ���main����������Ľṹ��
     */
    void                 *(*create_main_conf)(ngx_conf_t *cf);
    /* ������main����������֮��ص� */
    char                 *(*init_main_conf)(ngx_conf_t *cf, void *conf);

    /* �������ڴ洢streamģ��srv����������Ľṹ�� */
    void                 *(*create_srv_conf)(ngx_conf_t *cf);
    /*
     * create_srv_conf�����Ľṹ����Ҫ�洢�����������ͬʱ������main��srv�С�
     * merge_srv_conf�������԰ѳ�����main�����е�������ֵ�ϲ���srv������������
     */
    char                 *(*merge_srv_conf)(ngx_conf_t *cf, void *prev,
                                            void *conf);
} ngx_stream_module_t;


#define NGX_STREAM_MODULE       0x4d525453     /* "STRM" */

#define NGX_STREAM_MAIN_CONF    0x02000000
#define NGX_STREAM_SRV_CONF     0x04000000
#define NGX_STREAM_UPS_CONF     0x08000000


#define NGX_STREAM_MAIN_CONF_OFFSET  offsetof(ngx_stream_conf_ctx_t, main_conf)
#define NGX_STREAM_SRV_CONF_OFFSET   offsetof(ngx_stream_conf_ctx_t, srv_conf)


#define ngx_stream_get_module_ctx(s, module)   (s)->ctx[module.ctx_index]
#define ngx_stream_set_ctx(s, c, module)       s->ctx[module.ctx_index] = c;
#define ngx_stream_delete_ctx(s, module)       s->ctx[module.ctx_index] = NULL;


#define ngx_stream_get_module_main_conf(s, module)                             \
    (s)->main_conf[module.ctx_index]
#define ngx_stream_get_module_srv_conf(s, module)                              \
    (s)->srv_conf[module.ctx_index]

#define ngx_stream_conf_get_module_main_conf(cf, module)                       \
    ((ngx_stream_conf_ctx_t *) cf->ctx)->main_conf[module.ctx_index]
#define ngx_stream_conf_get_module_srv_conf(cf, module)                        \
    ((ngx_stream_conf_ctx_t *) cf->ctx)->srv_conf[module.ctx_index]

#define ngx_stream_cycle_get_module_main_conf(cycle, module)                   \
    (cycle->conf_ctx[ngx_stream_module.index] ?                                \
        ((ngx_stream_conf_ctx_t *) cycle->conf_ctx[ngx_stream_module.index])   \
            ->main_conf[module.ctx_index]:                                     \
        NULL)


void ngx_stream_init_connection(ngx_connection_t *c);
void ngx_stream_close_connection(ngx_connection_t *c);


extern ngx_module_t  ngx_stream_module;
extern ngx_uint_t    ngx_stream_max_module;
extern ngx_module_t  ngx_stream_core_module;


#endif /* _NGX_STREAM_H_INCLUDED_ */


/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_UPSTREAM_ROUND_ROBIN_H_INCLUDED_
#define _NGX_HTTP_UPSTREAM_ROUND_ROBIN_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef struct ngx_http_upstream_rr_peer_s   ngx_http_upstream_rr_peer_t;

/* 
 * һ����˷�������Ӧ��������Ϣ(���һ����˷������ж��ip��ַ��
 * ��ô����ṹ���Ӧ�ľ�������һ��ip��ַ�����Ϣ) 
 */
struct ngx_http_upstream_rr_peer_s {
    struct sockaddr                *sockaddr;  // ip��port��Ϣ
    socklen_t                       socklen;
    ngx_str_t                       name;
    ngx_str_t                       server;  // server������������

    ngx_int_t                       current_weight;  // ��ǰȨ��
    ngx_int_t                       effective_weight;  // ��ЧȨ�أ���������ӽ�����ͻ�������
    ngx_int_t                       weight;  // �������ļ��л�ȡ������Ȩ��

    ngx_uint_t                      conns;  // ��¼�˺�˷�����ѡ�еĴ���

    ngx_uint_t                      fails;  // ��fail_timeoutʱ����ʧ�ܵĴ���
    time_t                          accessed;
    time_t                          checked;  // ��־һ��fail_timeout���ڿ�ʼ��ʱ��

    ngx_uint_t                      max_fails;  // �����ļ��л�ȡ
    time_t                          fail_timeout;  // �����ļ��л�ȡ

    ngx_uint_t                      down;          /* unsigned  down:1; */

#if (NGX_HTTP_SSL)
    void                           *ssl_session;
    int                             ssl_session_len;
#endif

    /* ���һ��upstream�����ж��server���������nextָ�����Щserver������Ϣ�������� */
    ngx_http_upstream_rr_peer_t    *next;

#if (NGX_HTTP_UPSTREAM_ZONE)
    ngx_atomic_t                    lock;
#endif
};


typedef struct ngx_http_upstream_rr_peers_s  ngx_http_upstream_rr_peers_t;

struct ngx_http_upstream_rr_peers_s {
    /* һ��upstream�������зǱ��ݷ������ĸ���(��ip��������������������һ�������������ж��ip) */
    ngx_uint_t                      number;

#if (NGX_HTTP_UPSTREAM_ZONE)
    ngx_slab_pool_t                *shpool;
    ngx_atomic_t                    rwlock;
    ngx_http_upstream_rr_peers_t   *zone_next;
#endif

    ngx_uint_t                      total_weight;  // һ��upsteam�������зǱ��ݷ���������Ȩ��(��ip����)

    unsigned                        single:1;  // һ��upstream���Ƿ�ֻ��һ����˷������ı�־λ
    unsigned                        weighted:1;  // �Ƿ��Զ�����ÿ����˷�����������Ȩ��

    ngx_str_t                      *name;  // upstreamָ��������������

    ngx_http_upstream_rr_peers_t   *next;  //����һ��upstream�������б��ݷ�������ɵ��б�

    /* �洢��һ��upstream�������з�������ɵ��б�(���ݺͷǱ��ݷ������ֿ���֯) */
    ngx_http_upstream_rr_peer_t    *peer;
};


#if (NGX_HTTP_UPSTREAM_ZONE)

#define ngx_http_upstream_rr_peers_rlock(peers)                               \
                                                                              \
    if (peers->shpool) {                                                      \
        ngx_rwlock_rlock(&peers->rwlock);                                     \
    }

#define ngx_http_upstream_rr_peers_wlock(peers)                               \
                                                                              \
    if (peers->shpool) {                                                      \
        ngx_rwlock_wlock(&peers->rwlock);                                     \
    }

#define ngx_http_upstream_rr_peers_unlock(peers)                              \
                                                                              \
    if (peers->shpool) {                                                      \
        ngx_rwlock_unlock(&peers->rwlock);                                    \
    }


#define ngx_http_upstream_rr_peer_lock(peers, peer)                           \
                                                                              \
    if (peers->shpool) {                                                      \
        ngx_rwlock_wlock(&peer->lock);                                        \
    }

#define ngx_http_upstream_rr_peer_unlock(peers, peer)                         \
                                                                              \
    if (peers->shpool) {                                                      \
        ngx_rwlock_unlock(&peer->lock);                                       \
    }

#else

#define ngx_http_upstream_rr_peers_rlock(peers)
#define ngx_http_upstream_rr_peers_wlock(peers)
#define ngx_http_upstream_rr_peers_unlock(peers)
#define ngx_http_upstream_rr_peer_lock(peers, peer)
#define ngx_http_upstream_rr_peer_unlock(peers, peer)

#endif

/*
 * �洢�������ӱ���ʹ�õĺ�˷�������������з�������ɵ��б��Լ�ָʾ���к�˷������Ƿ�ѡ�й���
 * λͼ��
 */
typedef struct {
    ngx_http_upstream_rr_peers_t   *peers;  // �����˷������б�Ķ���
    ngx_http_upstream_rr_peer_t    *current;  // ��ǰ��ָ��ĺ�˷���������

    /* 
     * ָ���ʾ��˷������Ƿ�ѡ�е�λͼ��ַ�������˷���������С��uintptr_t���͵�λ����
     * ��ָ��data��ַ������������
     */
    uintptr_t                      *tried;
    /* �����˷���������С��uintptr_t���͵�λ��������data�����λͼ����ʱtriedָ��data��ַ */
    uintptr_t                       data;
} ngx_http_upstream_rr_peer_data_t;


ngx_int_t ngx_http_upstream_init_round_robin(ngx_conf_t *cf,
    ngx_http_upstream_srv_conf_t *us);
ngx_int_t ngx_http_upstream_init_round_robin_peer(ngx_http_request_t *r,
    ngx_http_upstream_srv_conf_t *us);
ngx_int_t ngx_http_upstream_create_round_robin_peer(ngx_http_request_t *r,
    ngx_http_upstream_resolved_t *ur);
ngx_int_t ngx_http_upstream_get_round_robin_peer(ngx_peer_connection_t *pc,
    void *data);
void ngx_http_upstream_free_round_robin_peer(ngx_peer_connection_t *pc,
    void *data, ngx_uint_t state);

#if (NGX_HTTP_SSL)
ngx_int_t
    ngx_http_upstream_set_round_robin_peer_session(ngx_peer_connection_t *pc,
    void *data);
void ngx_http_upstream_save_round_robin_peer_session(ngx_peer_connection_t *pc,
    void *data);
#endif


#endif /* _NGX_HTTP_UPSTREAM_ROUND_ROBIN_H_INCLUDED_ */

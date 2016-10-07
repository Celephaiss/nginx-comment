
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_STREAM_UPSTREAM_ROUND_ROBIN_H_INCLUDED_
#define _NGX_STREAM_UPSTREAM_ROUND_ROBIN_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>


typedef struct ngx_stream_upstream_rr_peer_s   ngx_stream_upstream_rr_peer_t;

/* 
 * һ����˷�������Ӧ��������Ϣ(���һ����˷������ж��ip��ַ��
 * ��ô����ṹ���Ӧ�ľ�������һ��ip��ַ�����Ϣ) �����Ϣ�ڷ������й����лᱻ����
 * �����������ͻ���������Ҫ���Ӻ�˷���������һ���ͻ�������ʹ�ø��ؾ����㷨ѡ��һ̨
 * ����������ô��������������������Ϣ�ᱻ���£����統ǰȨ�أ���ЧȨ�صȡ���Щ��Ӱ��
 * �ڶ����ͻ�������ѡ���˷�������
 */

struct ngx_stream_upstream_rr_peer_s {
    struct sockaddr                 *sockaddr;  // ��˷�����ip��port��Ϣ
    socklen_t                        socklen;
    ngx_str_t                        name;
    ngx_str_t                        server;

    ngx_int_t                        current_weight;  // ��ǰȨ��
    ngx_int_t                        effective_weight;  // ��ЧȨ��
    ngx_int_t                        weight;  // ����Ȩ��

    ngx_uint_t                       conns;  // ��¼�˺�˷�����ѡ�еĴ���

    ngx_uint_t                       fails;  // ʧ�ܴ���
    /* ѡȡ�ĺ�˷������쳣�����accessedʱ��Ϊ��ǰѡȡ��˷�������ʱ���⵽�쳣��ʱ�� */
    time_t                           accessed;
    
    /*
     *     fail_timeoutʱ���ڷ��ʺ�˷��������ִ���Ĵ������ڵ���max_fails������Ϊ�÷����������ã�
     * ��ô����������ˣ���˸÷������ָֻ�����ô�жϼ����?
     *     ��:checked�������ʱ�䣬����ĳ��ʱ���fail_timeout���ʱ����ʧЧ�ˣ������fail_timeout
     * ʱ��ι��˺󣬻�����peer->checked����ô�ֿ�����̽�÷������ˣ�

     * 1�����server��ʧ�ܴ�����peers->peer[i].fails��û�дﵽ��max_fails�����õ����ʧ�ܴ�����
     * ���server����Ч�ġ�
     * 2�����server�Ѿ��ﵽ��max_fails�����õ����ʧ�ܴ���������һʱ�̿�ʼ������fail_timeout 
     * �����õ�ʱ����ڣ� server����Ч�ġ�
     * 3����server��ʧ�ܴ�����peers->peer[i].fails��Ϊ����ʧ�ܴ��������������ڵ�ʱ��(���һ��
     * ѡ�ٸ÷�����ʧ��)������fail_timeout �����õ�ʱ��Σ� ����peers->peer[i].fails =0��ʹ��
     * ��server������Ч��
     */
    time_t                           checked;

    ngx_uint_t                       max_fails;
    
    /*
     * �����ʱ���ڲ�����max_fails�����ô�С��ʧ�ܳ������������������������ܲ����ã�
     */
    time_t                           fail_timeout;

    ngx_uint_t                       down;         /* unsigned  down:1; */

#if (NGX_STREAM_SSL)
    void                            *ssl_session;
    int                              ssl_session_len;
#endif

    ngx_stream_upstream_rr_peer_t   *next;

#if (NGX_STREAM_UPSTREAM_ZONE)
    ngx_atomic_t                     lock;
#endif
};


typedef struct ngx_stream_upstream_rr_peers_s  ngx_stream_upstream_rr_peers_t;

struct ngx_stream_upstream_rr_peers_s {
    ngx_uint_t                       number;  // upstream�����õķǱ��ݷ�����������(��ip����)

#if (NGX_STREAM_UPSTREAM_ZONE)
    ngx_slab_pool_t                 *shpool;
    ngx_atomic_t                     rwlock;
    ngx_stream_upstream_rr_peers_t  *zone_next;
#endif

    ngx_uint_t                       total_weight;  // upstream�����õķǱ��ݷ���������Ȩ��(��ip����)

    unsigned                         single:1;  // upstream�����Ƿ�ֻ��һ����˷������ı�־
    unsigned                         weighted:1;  // �Ƿ��Զ���������Ȩ�صı�־

    ngx_str_t                       *name;  // upstreamָ������������

    /* ����upsteam�������б��ݷ�������ɵ��б�Ķ��� */
    ngx_stream_upstream_rr_peers_t  *next;

    /* �洢��һ��upstream�������з�������ɵ��б�(���ݺͷǱ��ݷ������ֿ���֯) */
    ngx_stream_upstream_rr_peer_t   *peer;
};


#if (NGX_STREAM_UPSTREAM_ZONE)

#define ngx_stream_upstream_rr_peers_rlock(peers)                             \
                                                                              \
    if (peers->shpool) {                                                      \
        ngx_rwlock_rlock(&peers->rwlock);                                     \
    }

#define ngx_stream_upstream_rr_peers_wlock(peers)                             \
                                                                              \
    if (peers->shpool) {                                                      \
        ngx_rwlock_wlock(&peers->rwlock);                                     \
    }

#define ngx_stream_upstream_rr_peers_unlock(peers)                            \
                                                                              \
    if (peers->shpool) {                                                      \
        ngx_rwlock_unlock(&peers->rwlock);                                    \
    }


#define ngx_stream_upstream_rr_peer_lock(peers, peer)                         \
                                                                              \
    if (peers->shpool) {                                                      \
        ngx_rwlock_wlock(&peer->lock);                                        \
    }

#define ngx_stream_upstream_rr_peer_unlock(peers, peer)                       \
                                                                              \
    if (peers->shpool) {                                                      \
        ngx_rwlock_unlock(&peer->lock);                                       \
    }

#else

#define ngx_stream_upstream_rr_peers_rlock(peers)
#define ngx_stream_upstream_rr_peers_wlock(peers)
#define ngx_stream_upstream_rr_peers_unlock(peers)
#define ngx_stream_upstream_rr_peer_lock(peers, peer)
#define ngx_stream_upstream_rr_peer_unlock(peers, peer)

#endif


typedef struct {
    /* ����upstream�������к�˷�����(���ݺͷǱ��ݷֿ���֯)��ɵ��б�Ķ��� */
    ngx_stream_upstream_rr_peers_t  *peers;
    /* ���κͺ�˷���������ʱѡ�еĺ�˷����� */
    ngx_stream_upstream_rr_peer_t   *current;

    /* 
     * ָ���ʾ��˷������Ƿ�ѡ�е�λͼ��ַ�������˷���������С��uintptr_t���͵�λ����
     * ��ָ��data��ַ������������
     */
    uintptr_t                       *tried;

    /* �����˷���������С��uintptr_t���͵�λ��������data�����λͼ����ʱtriedָ��data��ַ */
    uintptr_t                        data;
} ngx_stream_upstream_rr_peer_data_t;


ngx_int_t ngx_stream_upstream_init_round_robin(ngx_conf_t *cf,
    ngx_stream_upstream_srv_conf_t *us);
ngx_int_t ngx_stream_upstream_init_round_robin_peer(ngx_stream_session_t *s,
    ngx_stream_upstream_srv_conf_t *us);
ngx_int_t ngx_stream_upstream_get_round_robin_peer(ngx_peer_connection_t *pc,
    void *data);
void ngx_stream_upstream_free_round_robin_peer(ngx_peer_connection_t *pc,
    void *data, ngx_uint_t state);


#endif /* _NGX_STREAM_UPSTREAM_ROUND_ROBIN_H_INCLUDED_ */

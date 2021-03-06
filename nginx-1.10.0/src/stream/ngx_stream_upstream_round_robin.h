
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
 * 一个后端服务器对应的配置信息(如果一个后端服务器有多个ip地址，
 * 那么这个结构体对应的就是其中一个ip地址相关信息) 这个信息在服务运行过程中会被更新
 * 比如有两个客户端请求需要连接后端服务器，第一个客户端请求使用负载均衡算法选择一台
 * 服务器，那么这个服务器的下面这个信息会被更新，比如当前权重，有效权重等。这些会影响
 * 第二个客户端请求选择后端服务器。
 */

struct ngx_stream_upstream_rr_peer_s {
    struct sockaddr                 *sockaddr;  // 后端服务器ip和port信息
    socklen_t                        socklen;
    ngx_str_t                        name;
    ngx_str_t                        server;

    ngx_int_t                        current_weight;  // 当前权重
    ngx_int_t                        effective_weight;  // 有效权重
    ngx_int_t                        weight;  // 配置权重

    ngx_uint_t                       conns;  // 记录此后端服务器选中的次数

    ngx_uint_t                       fails;  // 失败次数
    /* 选取的后端服务器异常则更新accessed时间为当前选取后端服务器的时候检测到异常的时间 */
    time_t                           accessed;
    
    /*
     *     fail_timeout时间内访问后端服务器出现错误的次数大于等于max_fails，则认为该服务器不可用，
     * 那么如果不可用了，后端该服务器又恢复了怎么判断检测呢?
     *     答:checked用来检测时间，例如某个时间段fail_timeout这段时间后端失效了，当这个fail_timeout
     * 时间段过了后，会重置peer->checked，那么又可以试探该服务器了，

     * 1）如果server的失败次数（peers->peer[i].fails）没有达到了max_fails所设置的最大失败次数，
     * 则该server是有效的。
     * 2）如果server已经达到了max_fails所设置的最大失败次数，从这一时刻开始算起，在fail_timeout 
     * 所设置的时间段内， server是无效的。
     * 3）当server的失败次数（peers->peer[i].fails）为最大的失败次数，当距离现在的时间(最近一次
     * 选举该服务器失败)超过了fail_timeout 所设置的时间段， 则令peers->peer[i].fails =0，使得
     * 该server重新有效。
     */
    time_t                           checked;

    ngx_uint_t                       max_fails;
    
    /*
     * 在这个时间内产生了max_fails所设置大小的失败尝试连接请求后这个服务器可能不可用，
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
    ngx_uint_t                       number;  // upstream中配置的非备份服务器的数量(以ip计数)

#if (NGX_STREAM_UPSTREAM_ZONE)
    ngx_slab_pool_t                 *shpool;
    ngx_atomic_t                     rwlock;
    ngx_stream_upstream_rr_peers_t  *zone_next;
#endif

    ngx_uint_t                       total_weight;  // upstream中配置的非备份服务器的总权重(以ip计数)

    unsigned                         single:1;  // upstream块中是否只有一个后端服务器的标志
    unsigned                         weighted:1;  // 是否自定义了配置权重的标志

    ngx_str_t                       *name;  // upstream指令后面跟的名字

    /* 管理upsteam块中所有备份服务器组成的列表的对象 */
    ngx_stream_upstream_rr_peers_t  *next;

    /* 存储着一个upstream块内所有服务器组成的列表(备份和非备份服务器分开组织) */
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
    /* 管理upstream块内所有后端服务器(备份和非备份分开组织)组成的列表的对象 */
    ngx_stream_upstream_rr_peers_t  *peers;
    /* 本次和后端服务器建链时选中的后端服务器 */
    ngx_stream_upstream_rr_peer_t   *current;

    /* 
     * 指向表示后端服务器是否被选中的位图地址，如果后端服务器个数小于uintptr_t类型的位数，
     * 则指向data地址，否则按需申请
     */
    uintptr_t                       *tried;

    /* 如果后端服务器个数小于uintptr_t类型的位数，则用data来存放位图，此时tried指向data地址 */
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

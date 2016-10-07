
/*
 * Copyright (C) Maxim Dounin
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_stream.h>


static ngx_int_t ngx_stream_upstream_init_least_conn_peer(
    ngx_stream_session_t *s, ngx_stream_upstream_srv_conf_t *us);
static ngx_int_t ngx_stream_upstream_get_least_conn_peer(
    ngx_peer_connection_t *pc, void *data);
static char *ngx_stream_upstream_least_conn(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);


static ngx_command_t  ngx_stream_upstream_least_conn_commands[] = {

    { ngx_string("least_conn"),
      NGX_STREAM_UPS_CONF|NGX_CONF_NOARGS,
      ngx_stream_upstream_least_conn,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_stream_module_t  ngx_stream_upstream_least_conn_module_ctx = {
    NULL,                                    /* postconfiguration */

    NULL,                                    /* create main configuration */
    NULL,                                    /* init main configuration */

    NULL,                                    /* create server configuration */
    NULL,                                    /* merge server configuration */
};


ngx_module_t  ngx_stream_upstream_least_conn_module = {
    NGX_MODULE_V1,
    &ngx_stream_upstream_least_conn_module_ctx, /* module context */
    ngx_stream_upstream_least_conn_commands, /* module directives */
    NGX_STREAM_MODULE,                       /* module type */
    NULL,                                    /* init master */
    NULL,                                    /* init module */
    NULL,                                    /* init process */
    NULL,                                    /* init thread */
    NULL,                                    /* exit thread */
    NULL,                                    /* exit process */
    NULL,                                    /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_int_t
ngx_stream_upstream_init_least_conn(ngx_conf_t *cf,
    ngx_stream_upstream_srv_conf_t *us)
{
    ngx_log_debug0(NGX_LOG_DEBUG_STREAM, cf->log, 0,
                   "init least conn");

    /*
     * �ú������ڹ����˷�������ɵ����������ص�ngx_stream_upstream_srv_conf_t��
     * ngx_stream_upstream_peer_t��data�ֶΡ�
     */
    if (ngx_stream_upstream_init_round_robin(cf, us) != NGX_OK) {
        return NGX_ERROR;
    }

    /* ��ʼ��least_conn���ؾ����㷨 */
    us->peer.init = ngx_stream_upstream_init_least_conn_peer;

    return NGX_OK;
}


static ngx_int_t
ngx_stream_upstream_init_least_conn_peer(ngx_stream_session_t *s,
    ngx_stream_upstream_srv_conf_t *us)
{
    ngx_log_debug0(NGX_LOG_DEBUG_STREAM, s->connection->log, 0,
                   "init least conn peer");

    /*
     * �ú���������get��free�������Լ�����ָʾ���к�˷������Ƿ�ѡ�����λͼ������֮�⣬
     * Ҳ�Ὣus->peer.data�ֶι��صĺ�˷������б����õ�s->upstream->peer.data�ϡ�
     */
    if (ngx_stream_upstream_init_round_robin_peer(s, us) != NGX_OK) {
        return NGX_ERROR;
    }

    /* ����ѡ�񱾴�������Ҫ���ӵĺ�˷���������ĺ��� */
    s->upstream->peer.get = ngx_stream_upstream_get_least_conn_peer;

    return NGX_OK;
}


static ngx_int_t
ngx_stream_upstream_get_least_conn_peer(ngx_peer_connection_t *pc, void *data)
{
    ngx_stream_upstream_rr_peer_data_t *rrp = data;

    time_t                           now;
    uintptr_t                        m;
    ngx_int_t                        rc, total;
    ngx_uint_t                       i, n, p, many;
    ngx_stream_upstream_rr_peer_t   *peer, *best;
    ngx_stream_upstream_rr_peers_t  *peers;

    ngx_log_debug1(NGX_LOG_DEBUG_STREAM, pc->log, 0,
                   "get least conn peer, try: %ui", pc->tries);

    /* ���ֻ������һ����˷���������ôֱ�ӵ���round-robin�㷨��ȡ�����������ӵĺ�˷����� */
    if (rrp->peers->single) {
        return ngx_stream_upstream_get_round_robin_peer(pc, rrp);
    }

    pc->connection = NULL;

    /* ��ȡ��ǰ����ʱ�� */
    now = ngx_time();

    peers = rrp->peers;

    ngx_stream_upstream_rr_peers_wlock(peers);

    best = NULL;
    total = 0;

#if (NGX_SUPPRESS_WARN)
    many = 0;
    p = 0;
#endif

    for (peer = peers->peer, i = 0;
         peer;
         peer = peer->next, i++)
    {

        /*
         * i��ʾ��ǰ�������ĺ�˷������ı�ţ���0��ʼ
         * n��ʾ��ǰ�������ĺ�˷��������ڵ�λͼ
         * m��ʾ��ǰ�������ĺ�˷�����������λͼ�ľ���λ��
         */
        n = i / (8 * sizeof(uintptr_t));
        m = (uintptr_t) 1 << i % (8 * sizeof(uintptr_t));

        /* ������α������ĺ�˷�����λͼ��ʾ֮ǰ�Ѿ�ѡ����ˣ���ô���β�ѡ */
        if (rrp->tried[n] & m) {
            continue;
        }

        /* ״̬Ϊdown�ķ�������ѡ */
        if (peer->down) {
            continue;
        }

        /* ��һ��fail_timeout������ʧ�ܴ����ﵽmax_fails�ķ�������ѡ */
        if (peer->max_fails
            && peer->fails >= peer->max_fails
            && now - peer->checked <= peer->fail_timeout)
        {
            continue;
        }

        /*
         * select peer with least number of connections; if there are
         * multiple peers with the same number of connections, select
         * based on round-robin
         */
        /*
         * ѡ������ʵķ�����������ж�̨��˷���������least-conn�㷨��������������
         * round-robin�㷨�����������Ķ�̨��������ѡ��һ̨��ǰȨ����ߵġ�
         */

        if (best == NULL
            || peer->conns * best->weight < best->conns * peer->weight)
        {
            best = peer;
            many = 0;
            p = i;  // ��¼��ѡΪbest�ĺ�˷��������

        } else if (peer->conns * best->weight == best->conns * peer->weight) {
            many = 1;  // �ж�̨��˷������ɹ�ѡ�����������round-robin�㷨��һ��ѡ��
        }
    }

    if (best == NULL) {
        ngx_log_debug0(NGX_LOG_DEBUG_STREAM, pc->log, 0,
                       "get least conn peer, no peer found");

        goto failed;
    }

    if (many) {
        ngx_log_debug0(NGX_LOG_DEBUG_STREAM, pc->log, 0,
                       "get least conn peer, many");

        /* 
         * �ӵ�һ����ָ��Ϊbest�ĺ�˷�������ʼ�������֮������к�˷���������
         * round-robin�㷨����Щ��������ѡ��һ������ʵġ�
         */
        for (peer = best, i = p;
             peer;
             peer = peer->next, i++)
        {
            n = i / (8 * sizeof(uintptr_t));
            m = (uintptr_t) 1 << i % (8 * sizeof(uintptr_t));

            if (rrp->tried[n] & m) {
                continue;
            }

            if (peer->down) {
                continue;
            }

            /*
             * ֻ������peer->conns * best->weight == best->conns * peer->weight�����ķ�������
             * ѡ����Ϊֻ������peer->conns * best->weight != best->conns * peer->weight�ķ�����
             * ���в�������ʵķ�������
             */
            if (peer->conns * best->weight != best->conns * peer->weight) {
                continue;
            }

            if (peer->max_fails
                && peer->fails >= peer->max_fails
                && now - peer->checked <= peer->fail_timeout)
            {
                continue;
            }

            peer->current_weight += peer->effective_weight;
            total += peer->effective_weight;

            if (peer->effective_weight < peer->weight) {
                peer->effective_weight++;
            }

            if (peer->current_weight > best->current_weight) {
                best = peer;
                p = i;
            }
        }
    }

    /* ���´˴�ѡ��ĺ�˷������ĵ�ǰȨ�� */
    best->current_weight -= total;

    if (now - best->checked > best->fail_timeout) {
        best->checked = now;
    }

    /* ��ȡ��ַ��Ϣ�������� */
    pc->sockaddr = best->sockaddr;
    pc->socklen = best->socklen;
    pc->name = &best->name;

    /* ������ѡ�д��� */
    best->conns++;

    rrp->current = best;

    /* ��¼λͼ */
    n = p / (8 * sizeof(uintptr_t));
    m = (uintptr_t) 1 << p % (8 * sizeof(uintptr_t));

    rrp->tried[n] |= m;

    ngx_stream_upstream_rr_peers_unlock(peers);

    return NGX_OK;

failed:

    /* �ӱ��ݷ������б���ѡ��һ̨����ʵķ����� */
    if (peers->next) {
        ngx_log_debug0(NGX_LOG_DEBUG_STREAM, pc->log, 0,
                       "get least conn peer, backup servers");

        rrp->peers = peers->next;

        /* �������б��ݷ�������Ҫ���ٸ�λͼ��ָʾ������ѡ����� */
        n = (rrp->peers->number + (8 * sizeof(uintptr_t) - 1))
                / (8 * sizeof(uintptr_t));

        for (i = 0; i < n; i++) {
            rrp->tried[i] = 0;
        }

        ngx_stream_upstream_rr_peers_unlock(peers);

        rc = ngx_stream_upstream_get_least_conn_peer(pc, rrp);

        if (rc != NGX_BUSY) {
            return rc;
        }

        ngx_stream_upstream_rr_peers_wlock(peers);
    }

    /* all peers failed, mark them as live for quick recovery */

    /*
     * ����ִ�е�����������еĺ�˷�����ѡ��ʧ���ˣ����ʱ����Ҫ����˷�������״̬��Ϣ����
     * ��ʹ�������¿���
     */
    for (peer = peers->peer; peer; peer = peer->next) {
        peer->fails = 0;
    }

    ngx_stream_upstream_rr_peers_unlock(peers);

    pc->name = peers->name;
    /* ����NGX_BUSY��������ngx_stream_proxy_connect�л�����˴����� */
    return NGX_BUSY;
}

/* least_connָ��Ľ���������һ��Ὣleast_connָ��������upstream����ʼ */
static char *
ngx_stream_upstream_least_conn(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_stream_upstream_srv_conf_t  *uscf;

    uscf = ngx_stream_conf_get_module_srv_conf(cf, ngx_stream_upstream_module);

    if (uscf->peer.init_upstream) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "load balancing method redefined");
    }

    /*
     * ���ó�ʼ��upstream���ؾ��ⷽ���Ļص��������ûص���������_upstream���Ƶ�
     * init_main_conf�ص�����ngx_stream_upstream_init_main_conf�е��á������������ļ�
     * �е�main����������֮��ᱻ����
     */
    uscf->peer.init_upstream = ngx_stream_upstream_init_least_conn;

    /*
     *     ���ڴ���upstream��������Ϣ�ṹ��Ĺ��ܱ�ǣ�����ط��е㲻����⣬��Ϊ��
     *     ����: ����upstream���ʱ��ͻ���������Щflags������upstream��������Ϣ�Ľṹ�壬
     * Ϊʲô��������Ҫ������������?����������ڽ�����least_conn֮��Ż���ã����ʱ��
     * �洢upstream��������Ϣ�Ľṹ���Ѿ��������ˣ�flagsҲ���ú��ˣ�����Ϊʲô��Ҫ��������?
     */
    uscf->flags = NGX_STREAM_UPSTREAM_CREATE
                  |NGX_STREAM_UPSTREAM_WEIGHT
                  |NGX_STREAM_UPSTREAM_MAX_FAILS
                  |NGX_STREAM_UPSTREAM_FAIL_TIMEOUT
                  |NGX_STREAM_UPSTREAM_DOWN
                  |NGX_STREAM_UPSTREAM_BACKUP;

    return NGX_CONF_OK;
}


/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_event_connect.h>

/* ����socket��connect��Զ�˷������������� */
ngx_int_t
ngx_event_connect_peer(ngx_peer_connection_t *pc)
{
    int                rc, type;
    ngx_int_t          event;
    ngx_err_t          err;
    ngx_uint_t         level;
    ngx_socket_t       s;
    ngx_event_t       *rev, *wev;
    ngx_connection_t  *c;

    /* �ӱ������Ӷ�Ӧ��upstream����ѡ��һ�����ʵĺ�˷����� */
    rc = pc->get(pc, pc->data);
    if (rc != NGX_OK) {
        return rc;
    }

    /* ��ȡ�������� */
    type = (pc->type ? pc->type : SOCK_STREAM);

    /* ����socket�ӿڻ�ȡһ��socket������ */
    s = ngx_socket(pc->sockaddr->sa_family, type, 0);

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, pc->log, 0, "%s socket %d",
                   (type == SOCK_STREAM) ? "stream" : "dgram", s);

    if (s == (ngx_socket_t) -1) {
        ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno,
                      ngx_socket_n " failed");
        return NGX_ERROR;
    }


    /* �����ӳ��л�ȡһ���������ӣ������һ���������ӵ����Ӷ��� */
    c = ngx_get_connection(s, pc->log);

    if (c == NULL) {
        if (ngx_close_socket(s) == -1) {
            ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno,
                          ngx_close_socket_n "failed");
        }

        return NGX_ERROR;
    }

    c->type = type;

    /* �������ӵĽ��ܻ�������С */
    if (pc->rcvbuf) {
        if (setsockopt(s, SOL_SOCKET, SO_RCVBUF,
                       (const void *) &pc->rcvbuf, sizeof(int)) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno,
                          "setsockopt(SO_RCVBUF) failed");
            goto failed;
        }
    }

    /* ����������Ϊ������ */
    if (ngx_nonblocking(s) == -1) {
        ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno,
                      ngx_nonblocking_n " failed");

        goto failed;
    }

    /* local�д洢����Nginx���ڵı�����ַ��Ϣ��Ϊ�գ���Ὣ�������󶨵�������ַ�� */
    if (pc->local) {
        if (bind(s, pc->local->sockaddr, pc->local->socklen) == -1) {
            ngx_log_error(NGX_LOG_CRIT, pc->log, ngx_socket_errno,
                          "bind(%V) failed", &pc->local->name);

            goto failed;
        }
    }

    if (type == SOCK_STREAM) {
        /* ���ö�д�������ķ��� */
        c->recv = ngx_recv;
        c->send = ngx_send;
        c->recv_chain = ngx_recv_chain;
        c->send_chain = ngx_send_chain;

        c->sendfile = 1;

        if (pc->sockaddr->sa_family == AF_UNIX) {
            c->tcp_nopush = NGX_TCP_NOPUSH_DISABLED;
            c->tcp_nodelay = NGX_TCP_NODELAY_DISABLED;

#if (NGX_SOLARIS)
            /* Solaris's sendfilev() supports AF_NCA, AF_INET, and AF_INET6 */
            c->sendfile = 0;
#endif
        }

    } else { /* type == SOCK_DGRAM */
        c->recv = ngx_udp_recv;
        c->send = ngx_send;
    }

    c->log_error = pc->log_error;

    rev = c->read;
    wev = c->write;

    rev->log = pc->log;
    wev->log = pc->log;

    /* ���������Ӷ������õ��������Ӷ����connection��Ա�� */
    pc->connection = c;

    c->number = ngx_atomic_fetch_add(ngx_connection_counter, 1);

    /* ��Nginx�����η����������Ӽ��뵽epoll��ͬʱ��ض�д�¼� */
    if (ngx_add_conn) {
        if (ngx_add_conn(c) == NGX_ERROR) {
            goto failed;
        }
    }

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, pc->log, 0,
                   "connect to %V, fd:%d #%uA", pc->name, s, c->number);

    /*
     * ����connect�������������η����������ӣ���Ϊsocket����������Ϊ�������׽��֣����connect��������
     * ������Ҳ��˸÷�������ʱ��һ���ͱ�ʾ�����η������ɹ�����������
     */
    rc = connect(s, pc->sockaddr, pc->socklen);

    /*
     * ���connect����-1������Ҫ���ݴ��������ж����ӽ�����״̬�������ʾNginx�����η����������ӳɹ�����
     */
    if (rc == -1) {
        err = ngx_socket_errno;

        /* ��������벻��EINPROGRESS���������ͻ��˽���ʧ�� */
        if (err != NGX_EINPROGRESS
#if (NGX_WIN32)
            /* Winsock returns WSAEWOULDBLOCK (NGX_EAGAIN) */
            && err != NGX_EAGAIN
#endif
            )
        {
            if (err == NGX_ECONNREFUSED
#if (NGX_LINUX)
                /*
                 * Linux returns EAGAIN instead of ECONNREFUSED
                 * for unix sockets if listen queue is full
                 */
                || err == NGX_EAGAIN
#endif
                || err == NGX_ECONNRESET
                || err == NGX_ENETDOWN
                || err == NGX_ENETUNREACH
                || err == NGX_EHOSTDOWN
                || err == NGX_EHOSTUNREACH)
            {
                level = NGX_LOG_ERR;

            } else {
                level = NGX_LOG_CRIT;
            }

            ngx_log_error(level, c->log, err, "connect() to %V failed",
                          pc->name);

            ngx_close_connection(c);
            pc->connection = NULL;

            return NGX_DECLINED;
        }
    }

    /*
     * ngx_add_conn����NULL,����������Ѿ������Ӷ�Ӧ�Ķ�д�¼����뵽epoll���ˣ������������connect����
     * �ķ���ֵΪ-1����ֱ�ӷ���NGX_AGAIN������Nginx��ʱ��û�������η������������ӣ���Ҫ�ȴ�epoll���Ƚ���
     * ��һ������(�ȵ����ӳɹ�����ʱepoll����ȸ�socket��������Ӧ��д�¼����������н�һ������)
     */
    if (ngx_add_conn) {
        if (rc == -1) {

            /* NGX_EINPROGRESS */

            return NGX_AGAIN;
        }

        /*
         * ����ִ�е��������Nginx��Զ�˷������������Ѿ��ɹ�������������Զ�˷��������������ˣ�����������Ҫ
         * ��д�¼�����Ϊ����
         */
        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, pc->log, 0, "connected");

        wev->ready = 1;

        return NGX_OK;
    }

    if (ngx_event_flags & NGX_USE_IOCP_EVENT) {

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, pc->log, ngx_socket_errno,
                       "connect(): %d", rc);

        if (ngx_blocking(s) == -1) {
            ngx_log_error(NGX_LOG_ALERT, pc->log, ngx_socket_errno,
                          ngx_blocking_n " failed");
            goto failed;
        }

        /*
         * FreeBSD's aio allows to post an operation on non-connected socket.
         * NT does not support it.
         *
         * TODO: check in Win32, etc. As workaround we can use NGX_ONESHOT_EVENT
         */

        rev->ready = 1;
        wev->ready = 1;

        return NGX_OK;
    }

    if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {

        /* kqueue */

        event = NGX_CLEAR_EVENT;

    } else {

        /* select, poll, /dev/poll */

        event = NGX_LEVEL_EVENT;
    }

    /*ʹ��epoll�Ļ�������Ա�Ե������ʽ�����Ӷ�Ӧ�Ķ��¼����뵽epoll��*/
    if (ngx_add_event(rev, NGX_READ_EVENT, event) != NGX_OK) {
        goto failed;
    }

    /*
     * connect����-1,���Ҵ�����ΪEINPROGRESS������Nginx��ʱ��û����Զ�˷������������ӣ�������Ҫ��
     * ���Ӷ�Ӧ��д�¼����뵽epoll�У��ȵ����ӳɹ�����������epoll���Ƚ���д�¼��Ľ�һ������
     */
    if (rc == -1) {

        /* NGX_EINPROGRESS */

        if (ngx_add_event(wev, NGX_WRITE_EVENT, event) != NGX_OK) {
            goto failed;
        }

        return NGX_AGAIN;
    }

    /*
     * ����ִ�е��������Nginx��Զ�˷������������Ѿ��ɹ�������������Զ�˷��������������ˣ�����������Ҫ
     * ��д�¼�����Ϊ����
     */
    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, pc->log, 0, "connected");

    wev->ready = 1;

    return NGX_OK;

failed:

    ngx_close_connection(c);
    pc->connection = NULL;

    return NGX_ERROR;
}


ngx_int_t
ngx_event_get_peer(ngx_peer_connection_t *pc, void *data)
{
    return NGX_OK;
}

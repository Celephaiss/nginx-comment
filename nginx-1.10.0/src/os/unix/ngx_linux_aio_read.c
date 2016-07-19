
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


extern int            ngx_eventfd;
extern aio_context_t  ngx_aio_ctx;


static void ngx_file_aio_event_handler(ngx_event_t *ev);


static int
io_submit(aio_context_t ctx, long n, struct iocb **paiocb)
{
    return syscall(SYS_io_submit, ctx, n, paiocb);
}

/*
 * ��ʼ���ļ��첽io����,����fileΪҪ��ȡ��file�ļ�����file->aioΪ���ļ����첽io����
 */
ngx_int_t
ngx_file_aio_init(ngx_file_t *file, ngx_pool_t *pool)
{
    ngx_event_aio_t  *aio;

    /*�����ڴ�*/
    aio = ngx_pcalloc(pool, sizeof(ngx_event_aio_t));
    if (aio == NULL) {
        return NGX_ERROR;
    }

    aio->file = file;  //fileΪҪ��ȡ��file�ļ�����
    aio->fd = file->fd;  //aio->fd��ΪҪ�����ļ�������
    
    /*
     * �����ｫ�첽io�¼���data��Ա��ֵΪ�첽io���������ngx_epoll_eventfd_handler()��
     * ngx_file_aio_event_handler()��������
     */
    aio->event.data = aio;
    
    aio->event.ready = 1;
    aio->event.log = file->log;

    file->aio = aio;

    return NGX_OK;
}

/*Nginx��װ���첽io�¼��ύ����*/
ssize_t
ngx_file_aio_read(ngx_file_t *file, u_char *buf, size_t size, off_t offset,
    ngx_pool_t *pool)
{
    ngx_err_t         err;
    struct iocb      *piocb[1];
    ngx_event_t      *ev;
    ngx_event_aio_t  *aio;

    if (!ngx_file_aio) {
        return ngx_read_file(file, buf, size, offset);
    }

    /*ngx_event_aio_t��װ���첽io�������file->aioΪ�գ���Ҫ��ʼ��file->aio*/
    if (file->aio == NULL && ngx_file_aio_init(file, pool) != NGX_OK) {
        return NGX_ERROR;
    }

    aio = file->aio;
    ev = &aio->event;

    if (!ev->ready) {
        ngx_log_error(NGX_LOG_ALERT, file->log, 0,
                      "second aio post for \"%V\"", &file->name);
        return NGX_AGAIN;
    }

    ngx_log_debug4(NGX_LOG_DEBUG_CORE, file->log, 0,
                   "aio complete:%d @%O:%uz %V",
                   ev->complete, offset, size, &file->name);

    if (ev->complete) {
        ev->active = 0;
        ev->complete = 0;

        if (aio->res >= 0) {
            ngx_set_errno(0);
            return aio->res;
        }

        ngx_set_errno(-aio->res);

        ngx_log_error(NGX_LOG_CRIT, file->log, ngx_errno,
                      "aio read \"%s\" failed", file->name.data);

        return NGX_ERROR;
    }

    /*�ύ�첽�¼�֮ǰҪ��ʼ���ṹ��struct iocb*/
    ngx_memzero(&aio->aiocb, sizeof(struct iocb));

    /*
     * ��struct iocb��aio_data��Ա��ֵΪ�첽io�¼����������ύ�첽�¼�֮�󣬵ȸ��¼���ɣ���ͨ��io_getevents()
     * ��ȡ���¼��󣬶�Ӧ��struct io_event�ṹ���е�data��Ա�ͻ�ָ������¼���
     * struct iocb��aio_data��Ա��struct io_event��data��Աָ�����ͬһ������
     */
    aio->aiocb.aio_data = (uint64_t) (uintptr_t) ev;
    aio->aiocb.aio_lio_opcode = IOCB_CMD_PREAD;
    aio->aiocb.aio_fildes = file->fd;
    aio->aiocb.aio_buf = (uint64_t) (uintptr_t) buf;
    aio->aiocb.aio_nbytes = size;
    aio->aiocb.aio_offset = offset;
    aio->aiocb.aio_flags = IOCB_FLAG_RESFD;  //����ΪIOCB_FLAG_RESFD��ʾ�ں����첽io��������ʱͨ��eventfd֪ͨӦ�ó���
    aio->aiocb.aio_resfd = ngx_eventfd;  //�������eventfd������

    /*
     * ��io_getevents()�����л�ȡ�����첽io�¼�ʱ������øûص���������Nginx�в�����ֱ�ӵ��ã������Ƚ�����뵽
     * ngx_posted_event���У��ȱ�����������ɵ��첽io�¼��������ε��������¼��Ļص�����
     */
    ev->handler = ngx_file_aio_event_handler;

    piocb[0] = &aio->aiocb;

    /*�����첽io������뵽�첽io�������У��ȴ�io��ɣ��ں˻�ͨ��eventfd֪ͨӦ�ó���*/
    if (io_submit(ngx_aio_ctx, 1, piocb) == 1) {
        ev->active = 1;
        ev->ready = 0;
        ev->complete = 0;

        return NGX_AGAIN;
    }

    err = ngx_errno;

    if (err == NGX_EAGAIN) {
        return ngx_read_file(file, buf, size, offset);
    }

    ngx_log_error(NGX_LOG_CRIT, file->log, err,
                  "io_submit(\"%V\") failed", &file->name);

    if (err == NGX_ENOSYS) {
        ngx_file_aio = 0;
        return ngx_read_file(file, buf, size, offset);
    }

    return NGX_ERROR;
}

/*�ļ��첽io�¼���ɺ�Ļص�����*/
static void
ngx_file_aio_event_handler(ngx_event_t *ev)
{
    ngx_event_aio_t  *aio;

    aio = ev->data;  //��ȡ�¼���Ӧ��data���󣬼�ngx_event_aio_t�����ngx_file_aio_init()�����г�ʼ����

    ngx_log_debug2(NGX_LOG_DEBUG_CORE, ev->log, 0,
                   "aio event handler fd:%d %V", aio->fd, &aio->file->name);

    /*
     * ����ص�����������ҵ��ģ��ʵ�ֵģ��ٸ����������http cacheģ�飬�����ngx_http_file_cache_aio_read()������
     * ������ngx_file_aio_read()������Ϊngx_http_cache_aio_event_handler()����ҵ���߼��Ĵ���ΪʲôҪ�ڵ�����
     * ngx_file_aio_read()֮���������أ���Ϊ����ҵ��ģ��һ��ʼ��û��Ϊngx_file_t��������ngx_event_aio_t���󣬶�����
     * ngx_file_aio_read()�е���ngx_file_aio_init()���г�ʼ���ġ�
     */
    aio->handler(ev);
}

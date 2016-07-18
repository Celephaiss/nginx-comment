
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

 /*
  * *****************************************epoll����***********************************************
  * ��Linux�ں�������һ�����׵��ļ�ϵͳ������epoll_create��������һ��epoll����(��epoll�ļ�ϵͳ�и�������������Դ)
  * Ȼ���ں��ʵ�ʱ�����epoll_ctl��epoll��������ӡ��޸Ļ���ɾ���¼���������epoll_wait�ռ��Ѿ��������¼�
  */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#if (NGX_TEST_BUILD_EPOLL)

/* epoll declarations */
/* ��ʾ��Ӧ�������������ݿ��Զ���(tcp���ӵ�Զ�������ر�����Ҳ�൱�ڿɶ��¼�����Ϊ��Ҫ����������fin��)*/
#define EPOLLIN        0x001

/*��ʾ��Ӧ���������н�����������Ҫ��*/
#define EPOLLPRI       0x002

/*��ʾ��Ӧ�������Ͽ���д�����ݷ���(���������η����������������tcp���ӣ����ӽ����ɹ����¼�Ҳ�൱�ڿ�д�¼�)*/
#define EPOLLOUT       0x004

#define EPOLLRDNORM    0x040
#define EPOLLRDBAND    0x080
#define EPOLLWRNORM    0x100
#define EPOLLWRBAND    0x200
#define EPOLLMSG       0x400

/*��ʾ��Ӧ�����ӷ�������*/
#define EPOLLERR       0x008

/*��ʾ��Ӧ�����ӱ�����*/
#define EPOLLHUP       0x010

/*��ʾtcp���ӵ�Զ�˹رջ��߰�ر�����*/
#define EPOLLRDHUP     0x2000

/*��ʾ���¼��Ĵ�����ʽ����Ϊ��Ե����(ET)��ϵͳĬ��Ϊˮƽ����(LT)*/
#define EPOLLET        0x80000000

/*��ʾ����¼�ֻ����һ�Σ��´���Ҫ����ʱ��Ҫ���¼���epoll*/
#define EPOLLONESHOT   0x40000000

#define EPOLL_CTL_ADD  1
#define EPOLL_CTL_DEL  2
#define EPOLL_CTL_MOD  3

typedef union epoll_data {
    void         *ptr;
    int           fd;
    uint32_t      u32;
    uint64_t      u64;
} epoll_data_t;

struct epoll_event {
    uint32_t      events;
    epoll_data_t  data;
};


int epoll_create(int size);

int epoll_create(int size)
{
    return -1;
}


int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);

int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    return -1;
}


int epoll_wait(int epfd, struct epoll_event *events, int nevents, int timeout);

int epoll_wait(int epfd, struct epoll_event *events, int nevents, int timeout)
{
    return -1;
}

#if (NGX_HAVE_EVENTFD)
#define SYS_eventfd       323
#endif

#if (NGX_HAVE_FILE_AIO)

#define SYS_io_setup      245
#define SYS_io_destroy    246
#define SYS_io_getevents  247

typedef u_int  aio_context_t;

struct io_event {
    uint64_t  data;  /* the data field from the iocb */
    uint64_t  obj;   /* what iocb this event came from */
    int64_t   res;   /* result code for this event */
    int64_t   res2;  /* secondary result */
};


#endif
#endif /* NGX_TEST_BUILD_EPOLL */

/*epollģ�����ڴ洢����������Ľṹ��*/
typedef struct {
    ngx_uint_t  events;
    ngx_uint_t  aio_requests;
} ngx_epoll_conf_t;


static ngx_int_t ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer);
#if (NGX_HAVE_EVENTFD)
static ngx_int_t ngx_epoll_notify_init(ngx_log_t *log);
static void ngx_epoll_notify_handler(ngx_event_t *ev);
#endif
static void ngx_epoll_done(ngx_cycle_t *cycle);
static ngx_int_t ngx_epoll_add_event(ngx_event_t *ev, ngx_int_t event,
    ngx_uint_t flags);
static ngx_int_t ngx_epoll_del_event(ngx_event_t *ev, ngx_int_t event,
    ngx_uint_t flags);
static ngx_int_t ngx_epoll_add_connection(ngx_connection_t *c);
static ngx_int_t ngx_epoll_del_connection(ngx_connection_t *c,
    ngx_uint_t flags);
#if (NGX_HAVE_EVENTFD)
static ngx_int_t ngx_epoll_notify(ngx_event_handler_pt handler);
#endif
static ngx_int_t ngx_epoll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer,
    ngx_uint_t flags);

#if (NGX_HAVE_FILE_AIO)
static void ngx_epoll_eventfd_handler(ngx_event_t *ev);
#endif

static void *ngx_epoll_create_conf(ngx_cycle_t *cycle);
static char *ngx_epoll_init_conf(ngx_cycle_t *cycle, void *conf);

/*
 * ep ��ʾ����epoll���
 * event_list ��ʾ���������洢�ں˷��صľ����¼�
 * nevents ��ʾ����epoll_waitһ�ο��Է��ص�����¼���Ŀ
 */
static int                  ep = -1;
static struct epoll_event  *event_list;  //���ڽ���epoll_waitϵͳ����ʱ�����ں�̬�¼�
static ngx_uint_t           nevents;  //����epoll_waitϵͳ����ʱһ�������Է��ص��¼�����

#if (NGX_HAVE_EVENTFD)
static int                  notify_fd = -1;
static ngx_event_t          notify_event;
static ngx_connection_t     notify_conn;
#endif

#if (NGX_HAVE_FILE_AIO)

int                         ngx_eventfd = -1;  //�ں��ļ��첽io��Ӧ������������eventfdϵͳ���ø�ֵ
aio_context_t               ngx_aio_ctx = 0;   //�ں��ļ��첽io������

static ngx_event_t          ngx_eventfd_event; //�ں��ļ��첽io��Ӧ�����Ӷ���Ķ��¼�
static ngx_connection_t     ngx_eventfd_conn;  //�ں��ļ��첽io��Ӧ�����Ӷ���

#endif

static ngx_str_t      epoll_name = ngx_string("epoll");

/*epoll�¼�ģ��֧�ֵ�������ָ��*/
static ngx_command_t  ngx_epoll_commands[] = {

    /*
     * �ڵ���epoll_waitʱ�����ɵڶ��������͵�������������Linux�ں�һ�������Է��ض��ٸ��¼�������������ʾ,
     * ����һ��epoll_wait�����Է��ص��¼�������Ȼ��Ҳ��Ԥ������ô���epoll_event�ṹ�����ڴ洢�¼�
     */
    { ngx_string("epoll_events"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_epoll_conf_t, events),
      NULL },

    /*ָ���ڿ����첽I/O��ʹ��io_setupϵͳ���ó�ʼ���첽I/O�����Ļ���ʱ����ʼ������첽I/O�¼�����*/
    { ngx_string("worker_aio_requests"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_epoll_conf_t, aio_requests),
      NULL },

      ngx_null_command
};

/*ngx_epoll_moduleģ��ʵ�ֵ��¼�ģ���ͨ�ýӿ�*/
ngx_event_module_t  ngx_epoll_module_ctx = {
    &epoll_name,
    ngx_epoll_create_conf,               /* create configuration */
    ngx_epoll_init_conf,                 /* init configuration */

    {
        ngx_epoll_add_event,             /* add an event */
        ngx_epoll_del_event,             /* delete an event */
        ngx_epoll_add_event,             /* enable an event */
        ngx_epoll_del_event,             /* disable an event */
        ngx_epoll_add_connection,        /* add an connection */
        ngx_epoll_del_connection,        /* delete an connection */
#if (NGX_HAVE_EVENTFD)
        ngx_epoll_notify,                /* trigger a notify */
#else
        NULL,                            /* trigger a notify */
#endif
        ngx_epoll_process_events,        /* process the events */
        ngx_epoll_init,                  /* init the events */
        ngx_epoll_done,                  /* done the events */
    }
};

/*ngx_epoll_moduleʵ�ֵ�ģ��ͨ�ýӿ�*/
ngx_module_t  ngx_epoll_module = {
    NGX_MODULE_V1,
    &ngx_epoll_module_ctx,               /* module context */
    ngx_epoll_commands,                  /* module directives */
    NGX_EVENT_MODULE,                    /* module type */
    NULL,                                /* init master */
    NULL,                                /* init module */
    NULL,                                /* init process */
    NULL,                                /* init thread */
    NULL,                                /* exit thread */
    NULL,                                /* exit process */
    NULL,                                /* exit master */
    NGX_MODULE_V1_PADDING
};


#if (NGX_HAVE_FILE_AIO)

/*
 * We call io_setup(), io_destroy() io_submit(), and io_getevents() directly
 * as syscalls instead of libaio usage, because the library header file
 * supports eventfd() since 0.3.107 version only.
 */
/*
 * ��ʼ���ļ��첽io�������ģ�ִ�гɹ���ctx���Ƿ����������������������첽io�����Ľ����ٿ��Դ���nr_reqs���¼�
 */
static int
io_setup(u_int nr_reqs, aio_context_t *ctx)
{
    return syscall(SYS_io_setup, nr_reqs, ctx);
}

/*�����ļ��첽io������*/
static int
io_destroy(aio_context_t ctx)
{
    return syscall(SYS_io_destroy, ctx);
}

/*
 * ������ɵ��ļ��첽io���������ж�ȡ����������ȡ[min_nr,nr]���¼���events��ִ����ɵ��¼�����,tmo��ֵ�ڻ�ȡ
 * min_nr���¼�ǰ�ĵȴ�ʱ��
 */
static int
io_getevents(aio_context_t ctx, long min_nr, long nr, struct io_event *events,
    struct timespec *tmo)
{
    return syscall(SYS_io_getevents, ctx, min_nr, nr, events, tmo);
}


static void
ngx_epoll_aio_init(ngx_cycle_t *cycle, ngx_epoll_conf_t *epcf)
{
    int                 n;
    struct epoll_event  ee;

#if (NGX_HAVE_SYS_EVENTFD_H)
    ngx_eventfd = eventfd(0, 0);  //����eventfd()ϵͳ���ÿ��Դ���һ��efd������
#else
    ngx_eventfd = syscall(SYS_eventfd, 0);
#endif

    if (ngx_eventfd == -1) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "eventfd() failed");
        ngx_file_aio = 0;
        return;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "eventfd: %d", ngx_eventfd);

    n = 1;

    /*����ngx_eventfdΪ������*/
    if (ioctl(ngx_eventfd, FIONBIO, &n) == -1) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "ioctl(eventfd, FIONBIO) failed");
        goto failed;
    }

    /*��ʼ���ļ��첽io�����ģ�aio_requests��ʾ���ٿ��Դ�����첽�ļ�io�¼�����*/
    if (io_setup(epcf->aio_requests, &ngx_aio_ctx) == -1) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                      "io_setup() failed");
        goto failed;
    }

    /*�����첽io���ʱ֪ͨ���¼�*/
    ngx_eventfd_event.data = &ngx_eventfd_conn;  //ngx_event_t->data��Աͨ�������¼���Ӧ�����Ӷ���
    ngx_eventfd_event.handler = ngx_epoll_eventfd_handler;
    ngx_eventfd_event.log = cycle->log;
    ngx_eventfd_event.active = 1;  //activeΪ1����ʾ��������ʵ�������Ҫ������뵽epoll������������1
    ngx_eventfd_conn.fd = ngx_eventfd;
    ngx_eventfd_conn.read = &ngx_eventfd_event;  //�ں��ļ��첽io��Ӧ�����Ӷ�����¼�Ϊngx_eventfd_event
    ngx_eventfd_conn.log = cycle->log;

    ee.events = EPOLLIN|EPOLLET;  //��ض��¼�
    ee.data.ptr = &ngx_eventfd_conn;

    /*���첽�ļ�io��֪ͨ�����������뵽epoll�����*/
    if (epoll_ctl(ep, EPOLL_CTL_ADD, ngx_eventfd, &ee) != -1) {
        return;
    }

    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                  "epoll_ctl(EPOLL_CTL_ADD, eventfd) failed");

    if (io_destroy(ngx_aio_ctx) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "io_destroy() failed");
    }

failed:

    if (close(ngx_eventfd) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "eventfd close() failed");
    }

    ngx_eventfd = -1;
    ngx_aio_ctx = 0;
    ngx_file_aio = 0;
}

#endif

/*
 * ��ʼ��epollģ��
 * 1.����epoll_create��������epoll����
 * 2.����event_list���飬���ڽ���epoll_wait����ʱ�����ں�̬�¼�
 * 3.��ʼ��eventfd(��������˵Ļ�)
 * 4.��ʼ���첽�ļ�IO(��������˵Ļ�)
 * 5.����epoll�Ĵ�����ʽΪETģʽ
 */
static ngx_int_t
ngx_epoll_init(ngx_cycle_t *cycle, ngx_msec_t timer)
{
    ngx_epoll_conf_t  *epcf;

	/*��ȡngx_epoll_moduleģ�����ڴ洢����������Ľṹ��*/
    epcf = ngx_event_get_conf(cycle->conf_ctx, ngx_epoll_module);

    if (ep == -1) {
		/*����epoll_createϵͳ���ô���epoll���*/
        ep = epoll_create(cycle->connection_n / 2);

        if (ep == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "epoll_create() failed");
            return NGX_ERROR;
        }

#if (NGX_HAVE_EVENTFD)
        if (ngx_epoll_notify_init(cycle->log) != NGX_OK) {
            ngx_epoll_module_ctx.actions.notify = NULL;
        }
#endif

#if (NGX_HAVE_FILE_AIO)

        ngx_epoll_aio_init(cycle, epcf);

#endif
    }

    if (nevents < epcf->events) {
        if (event_list) {
            ngx_free(event_list);
        }

		/*�������ڴ��epoll_wait���صľ����¼���ע���ڴ���ֱ�������ϵͳ�����*/
        event_list = ngx_alloc(sizeof(struct epoll_event) * epcf->events,
                               cycle->log);
        if (event_list == NULL) {
            return NGX_ERROR;
        }
    }

	/*��ʼ��nevents*/
    nevents = epcf->events;

    ngx_io = ngx_os_io;

	/*��ʼ��ȫ���¼�����ģ���actions�ص�����*/
    ngx_event_actions = ngx_epoll_module_ctx.actions;

#if (NGX_HAVE_CLEAR_EVENT)
	/*Ĭ�ϲ���ET������ʽ*/
    ngx_event_flags = NGX_USE_CLEAR_EVENT
#else
    ngx_event_flags = NGX_USE_LEVEL_EVENT
#endif
                      |NGX_USE_GREEDY_EVENT
                      |NGX_USE_EPOLL_EVENT;

    return NGX_OK;
}


#if (NGX_HAVE_EVENTFD)

static ngx_int_t
ngx_epoll_notify_init(ngx_log_t *log)
{
    struct epoll_event  ee;

#if (NGX_HAVE_SYS_EVENTFD_H)
    notify_fd = eventfd(0, 0);
#else
    notify_fd = syscall(SYS_eventfd, 0);
#endif

    if (notify_fd == -1) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno, "eventfd() failed");
        return NGX_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, log, 0,
                   "notify eventfd: %d", notify_fd);

    notify_event.handler = ngx_epoll_notify_handler;
    notify_event.log = log;
    notify_event.active = 1;

    notify_conn.fd = notify_fd;
    notify_conn.read = &notify_event;
    notify_conn.log = log;

    ee.events = EPOLLIN|EPOLLET;
    ee.data.ptr = &notify_conn;

    if (epoll_ctl(ep, EPOLL_CTL_ADD, notify_fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                      "epoll_ctl(EPOLL_CTL_ADD, eventfd) failed");

        if (close(notify_fd) == -1) {
            ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                            "eventfd close() failed");
        }

        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_epoll_notify_handler(ngx_event_t *ev)
{
    ssize_t               n;
    uint64_t              count;
    ngx_err_t             err;
    ngx_event_handler_pt  handler;

    if (++ev->index == NGX_MAX_UINT32_VALUE) {
        ev->index = 0;

        n = read(notify_fd, &count, sizeof(uint64_t));

        err = ngx_errno;

        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                       "read() eventfd %d: %z count:%uL", notify_fd, n, count);

        if ((size_t) n != sizeof(uint64_t)) {
            ngx_log_error(NGX_LOG_ALERT, ev->log, err,
                          "read() eventfd %d failed", notify_fd);
        }
    }

    handler = ev->data;
    handler(ev);
}

#endif

/*nginx�˳������ʱ����ã��ͷ��ڴ漰����ȫ�ֱ������ر�epoll�ļ����*/
static void
ngx_epoll_done(ngx_cycle_t *cycle)
{
	//�ر�epoll���
    if (close(ep) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "epoll close() failed");
    }

    ep = -1;

#if (NGX_HAVE_EVENTFD)

    if (close(notify_fd) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "eventfd close() failed");
    }

    notify_fd = -1;

#endif

#if (NGX_HAVE_FILE_AIO)

    if (ngx_eventfd != -1) {

        if (io_destroy(ngx_aio_ctx) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "io_destroy() failed");
        }

        if (close(ngx_eventfd) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "eventfd close() failed");
        }

        ngx_eventfd = -1;
    }

    ngx_aio_ctx = 0;

#endif

	/*�ͷ�event_list*/
    ngx_free(event_list);

    event_list = NULL;
    nevents = 0;
}

/*��epoll����ӻ����޸��¼�*/
static ngx_int_t
ngx_epoll_add_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    int                  op;
    uint32_t             events, prev;
    ngx_event_t         *e;
    ngx_connection_t    *c;
    struct epoll_event   ee;

    /*��ȡ�¼���Ӧ������*/
    c = ev->data;

    /*��������event����ȷ����ǰ�¼��Ƕ��¼�����д�¼���������events�Ǽ���EPOLLIN����EPOLLOUT��־λ*/
    events = (uint32_t) event;

    /*
     * ��Nginx��ͨ����epoll_ctl����ض�д�¼��ģ����ʱ��Ĳ�����Ҫ��������add��mod����һ��fd��һ��ע�ᵽepoll
     * ʱ����ʹ�õ���add������ʽ�����֮ǰ�����fd�Ѿ���ӹ���д�¼��ļ�أ���ô����Ҫͨ��mod���޸�ԭ���ļ�ط�ʽ
     * ���һ��fd���ظ�add���ᱨ��
     */
    /*
     * ����������˵��epoll_ctl��ע��֮����NginxΪ�˱��������������������Ҫ��epoll�����һ��fd�Ķ��¼����м��ʱ��
     * Nginx�ȿ������fd��Ӧ��д�¼���״̬��������fd��Ӧ��д�¼�����Ч�ģ���e->active��־λΪ1������֮ǰ���fd
     * ��NGX_WRITE_EVENT����(add)��epoll���ˣ���ʱֻ��Ҫʹ��mod��ʽ�޸����ط�ʽ���ɣ����ܲ���add��ʽ���������һ��fd
     * ��д�¼��ļ��Ҳ��һ����˼��
     */

    if (event == NGX_READ_EVENT) {
        e = c->write;
        prev = EPOLLOUT;
#if (NGX_READ_EVENT != EPOLLIN|EPOLLRDHUP)
        events = EPOLLIN|EPOLLRDHUP;
#endif

    } else {
        e = c->read;
        prev = EPOLLIN|EPOLLRDHUP;
#if (NGX_WRITE_EVENT != EPOLLOUT)
        events = EPOLLOUT;
#endif
    }

	/*
	 * e��ev�¼���Ӧ���ӵĶ�����д�¼�
	 * ev����Ƕ��¼�����eΪ��Ӧ��д�¼������eΪactive������ev��Ӧ��c�Ǵ��ڵģ���ʱopΪmodify
	 * ev�����д�¼�����eΪ��Ӧ�Ķ��¼������eΪactive������ev��Ӧ��c�Ǵ��ڵģ���ʱopΪmodify
	 * �෴�ģ����e����active�������������¼�
	 */
    if (e->active) {
        op = EPOLL_CTL_MOD;
        events |= prev;  //��֮ǰ��eventҲ���뵽events�У��¾��¼���־����Ҫ���м��

    } else {
        op = EPOLL_CTL_ADD;
    }

    /*����flags��events��־λ��*/
    ee.events = events | (uint32_t) flags;
    /*���¼���instance��־λ��ӵ��������һλ���ں����ж��¼��Ƿ����,�ڴ����¼���ʱ�������ж��¼��Ƿ��Ѿ�������*/
    ee.data.ptr = (void *) ((uintptr_t) c | ev->instance);

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "epoll add event: fd:%d op:%d ev:%08XD",
                   c->fd, op, ee.events);

    /*����epoll_ctl��epoll��������ӻ����޸��¼�*/
    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

	//��Ϊfd��Ӧ�ĸ��¼��Ѿ����뵽��epoll�У������Ϊ��Ծ
    ev->active = 1;
#if 0
    ev->oneshot = (flags & NGX_ONESHOT_EVENT) ? 1 : 0;
#endif

    return NGX_OK;
}

/*������epoll��ɾ�������޸��¼�*/
static ngx_int_t
ngx_epoll_del_event(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags)
{
    int                  op;
    uint32_t             prev;
    ngx_event_t         *e;
    ngx_connection_t    *c;
    struct epoll_event   ee;

    /*
     * when the file descriptor is closed, the epoll automatically deletes
     * it from its queue, so we do not need to delete explicitly the event
     * before the closing the file descriptor
     */

    if (flags & NGX_CLOSE_EVENT) {
        ev->active = 0;
        return NGX_OK;
    }

    /*��ȡ�¼���Ӧ������*/
    c = ev->data;

	/*�˴�ԭ��������add_event�е�ʵ��*/
    if (event == NGX_READ_EVENT) {
        e = c->write;
        prev = EPOLLOUT;

    } else {
        e = c->read;
        prev = EPOLLIN|EPOLLRDHUP;
    }

    /*
     * e->activeΪ1�������fd�Ѿ�ͨ����һ���¼����뵽epoll�ļ�����ˣ���Ϊ��������Ҫ�Ƴ����fd��Ӧ��event���͵��¼�,
     * ��ô������Ҫͨ��mod��ʽ�޸ģ�ֻ������fd���뵽epoll�еĳ���event֮����������͵��¼����ٸ����ӣ�������fd�Ѿ�
     * ��NGX_READ_EVENT��NGX_WRITE_EVENT�����뵽epoll������ˣ��˴���Ҫ�Ƴ�NGX_READ_EVENT����ô��ֻ��epoll������fd��
     * NGX_WRITE_EVENT���͵��¼�
     */
    if (e->active) {
        op = EPOLL_CTL_MOD;
        ee.events = prev | (uint32_t) flags;  //�������˴˴���Ҫ�Ƴ����¼�����֮�������֮ǰ�Ѿ����뵽epoll��ص��¼�����
        ee.data.ptr = (void *) ((uintptr_t) c | ev->instance);

    } else {
        op = EPOLL_CTL_DEL;
        ee.events = 0;
        ee.data.ptr = NULL;
    }

    ngx_log_debug3(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                   "epoll del event: fd:%d op:%d ev:%08XD",
                   c->fd, op, ee.events);

    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

	/*��Ϊ��fd�ĸ��¼��Ѿ���epoll���Ƴ��ˣ����Խ���fd��Ӧ�ĸ��¼���Ϊ����Ծ*/
    ev->active = 0;

    return NGX_OK;
}

/*��epoll�����һ���µ�����*/
static ngx_int_t
ngx_epoll_add_connection(ngx_connection_t *c)
{
    struct epoll_event  ee;

    /*�����Ӷ�Ӧ�Ķ�д�¼�������epoll��*/
    ee.events = EPOLLIN|EPOLLOUT|EPOLLET|EPOLLRDHUP;
    ee.data.ptr = (void *) ((uintptr_t) c | c->read->instance);

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "epoll add connection: fd:%d ev:%08XD", c->fd, ee.events);

    if (epoll_ctl(ep, EPOLL_CTL_ADD, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      "epoll_ctl(EPOLL_CTL_ADD, %d) failed", c->fd);
        return NGX_ERROR;
    }

	/* 
	 * �����Ӷ�Ӧ�Ķ���д�¼�������Ϊ��Ծ����Ϊ���һ�����ӵ�epoll����У���ô��ʾ������Ӷ�Ӧ�Ķ�д�¼���
	 * ����epoll�������
     */
    c->read->active = 1;
    c->write->active = 1;

    return NGX_OK;
}

/*��epoll���Ƴ���һ�����ӵļ��*/
static ngx_int_t
ngx_epoll_del_connection(ngx_connection_t *c, ngx_uint_t flags)
{
    int                 op;
    struct epoll_event  ee;

    /*
     * when the file descriptor is closed the epoll automatically deletes
     * it from its queue so we do not need to delete explicitly the event
     * before the closing the file descriptor
     */

    /*
     * ���flags������NGX_CLOSE_EVENT��˵�����Ӷ�Ӧ��fd�Ѿ��ر��ˣ���ô���ǾͲ���Ҫ����epoll_ctl�Ƴ���ϵͳ���Զ��Ƴ�
     * ���ʱ��ֻ��Ҫ�����Ӷ�Ӧ�Ķ�д�¼��ı�־λ���㼴��
     */
    if (flags & NGX_CLOSE_EVENT) {
        c->read->active = 0;
        c->write->active = 0;
        return NGX_OK;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, c->log, 0,
                   "epoll del connection: fd:%d", c->fd);

    /*ִ���Ƴ�����*/
    op = EPOLL_CTL_DEL;
    ee.events = 0;
    ee.data.ptr = NULL;

    if (epoll_ctl(ep, op, c->fd, &ee) == -1) {
        ngx_log_error(NGX_LOG_ALERT, c->log, ngx_errno,
                      "epoll_ctl(%d, %d) failed", op, c->fd);
        return NGX_ERROR;
    }

    /*����Ǵ�epoll���Ƴ�������ӣ���ô��Ҫ��������Ӷ�Ӧ�Ķ�д�¼���active��־λ����*/
    c->read->active = 0;
    c->write->active = 0;

    return NGX_OK;
}


#if (NGX_HAVE_EVENTFD)
/*��װ��eventfd��ص�writeϵͳ����*/
static ngx_int_t
ngx_epoll_notify(ngx_event_handler_pt handler)
{
    static uint64_t inc = 1;

    notify_event.data = handler;

    if ((size_t) write(notify_fd, &inc, sizeof(uint64_t)) != sizeof(uint64_t)) {
        ngx_log_error(NGX_LOG_ALERT, notify_event.log, ngx_errno,
                      "write() to eventfd %d failed", notify_fd);
        return NGX_ERROR;
    }

    return NGX_OK;
}

#endif

/*epoll�¼��������������ռ��ͷַ������¼�*/
static ngx_int_t
ngx_epoll_process_events(ngx_cycle_t *cycle, ngx_msec_t timer, ngx_uint_t flags)
{
    int                events;
    uint32_t           revents;
    ngx_int_t          instance, i;
    ngx_uint_t         level;
    ngx_err_t          err;
    ngx_event_t       *rev, *wev;
    ngx_queue_t       *queue;
    ngx_connection_t  *c;

    /* NGX_TIMER_INFINITE == INFTIM */

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "epoll timer: %M", timer);

    /*ͨ��ϵͳ����epoll_wait��ȡ�Ѿ��������¼�������ֵΪ��ȡ�ľ����¼����������Ϊ-1����ʾ���ó���*/
    events = epoll_wait(ep, event_list, (int) nevents, timer);

    err = (events == -1) ? ngx_errno : 0;

	//����ʱ��
    if (flags & NGX_UPDATE_TIME || ngx_event_timer_alarm) {
        ngx_time_update();
    }

    /*������*/
    if (err) {
        if (err == NGX_EINTR) {

            if (ngx_event_timer_alarm) {
                ngx_event_timer_alarm = 0;
                return NGX_OK;
            }

            level = NGX_LOG_INFO;

        } else {
            level = NGX_LOG_ALERT;
        }

        ngx_log_error(level, cycle->log, err, "epoll_wait() failed");
        return NGX_ERROR;
    }

	/*����������¼�Ϊ0����timer��ΪNGX_TIMER_INFINITE�������̷���*/
    if (events == 0) {
        if (timer != NGX_TIMER_INFINITE) {
            return NGX_OK;
        }

        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                      "epoll_wait() returned no events without timeout");
        return NGX_ERROR;
    }

	/*ѭ������epoll_wait���صľ����¼�*/
    for (i = 0; i < events; i++) {
        c = event_list[i].data.ptr;  //��ȡ�¼���Ӧ������

        instance = (uintptr_t) c & 1; //ȡ������¼���epoll�и��ӵ��¼���ʱ��־λinstance
        c = (ngx_connection_t *) ((uintptr_t) c & (uintptr_t) ~1);  //��ԭ���ӣ�ȥ�����һλ��instance��־λ

		/*��ȡ���¼�*/
        rev = c->read;

		/*�¼�����*/
        if (c->fd == -1 || rev->instance != instance) {

            /*
             * the stale event from a file descriptor
             * that was just closed in this iteration
             */

            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "epoll: stale event %p", c);
            continue;
        }

		/*��ȡ�¼�����*/
        revents = event_list[i].events;

        ngx_log_debug3(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "epoll: fd:%d ev:%04XD d:%p",
                       c->fd, revents, event_list[i].data.ptr);

        /*���ӳ��ִ���EPOLLHUP��ʾ�յ��˶Է����͵�rst���ġ���⵽����������ʱ��tcp�����п��ܻ�������δ����ȡ*/
        if (revents & (EPOLLERR|EPOLLHUP)) {
            ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "epoll_wait() error on fd:%d ev:%04XD",
                           c->fd, revents);
        }

#if 0
        if (revents & ~(EPOLLIN|EPOLLOUT|EPOLLERR|EPOLLHUP)) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                          "strange epoll_wait() events fd:%d ev:%04XD",
                          c->fd, revents);
        }
#endif

        /*
         * 1��������fd(����socket)����fd�����õȴ��¼���EPOLLIN ����EPOLLET |EPOLLIN 
         *     
         *    ���ڴ�socketֻ�����������ӣ�̸����д��������������ֻ�������ࡣ��Ĭ����LTģʽ����EPOLLLT |EPOLLIN��    
         *    ˵������������socket��Ҳ����EPOLLOUT�ȣ�Ҳ�������ֻ�����socket�����յ���������Ϣ��
         *
         * 2���ͻ��������ر�,��client ��close()����
         *     �Զ������رգ�������close()��shell��kill��ctr+c��������EPOLLIN��EPOLLRDHUP��
         * ���ǲ�����EPOLLERR��EPOLLHUP.
         *     server�ᱨĳ��sockfd�ɶ�����EPOLLIN���١�Ȼ��recvһ�£��������0�ٵ���epoll_ctl�е�
         * EPOLL_CTL_DEL , ͬʱclose(sockfd)��
         *     ��Щϵͳ���յ�һ��EPOLLRDHUP����Ȼ����������ò����ˡ�ֻ��ϧ����Щϵͳ��ⲻ����
         * ����ܼ��϶�EPOLLRDHUP�Ĵ����Ǿ������ܵ��ˡ�
         *
         * 3���ͻ����쳣�رգ�
         *     �ͻ����쳣�رգ��ᴥ��EPOLLERR��EPOLLHUP��������֪ͨ�������������ر�ʱ�����ִ��read���Է���0��
         * �쳣�Ͽ�ʱ��ⲻ����,��ʱ�������ٸ�һ���Ѿ��رյ�socketд����ʱ����������������׶Է������Ѿ��쳣�Ͽ��ˣ���Ҳ���ԣ���
         *     epoll�о������Ѿ��Ͽ���socketд���߶����ᷢ��EPOLLERR���������Ѿ��Ͽ���
         */

        /*
         * ������ӷ�������δ��EPOLLIN��EPOLLOUT����ʱ���Ǽ���EPOLLIN��EPOLLOUT���ڵ��ö�/д�¼���
         * �ص�����ʱ�ͻ�֪��Ϊʲô���ִ��� �������EPOLLIN��EPOLLOUT�������û�����ö�/д�¼���
         * �ص�����Ҳ���޷�����������ˡ�
         * ֻ���ڲ�ȡ�ж��������һ���Ѿ��رյ�socket������дһ���Ѿ��رյ�socket��ʱ�򣬲�֪���Է��Ƿ�ر��ˡ�
         * ���ʱ������Է��쳣�ر��ˣ�������EPOLLERR
         */
        if ((revents & (EPOLLERR|EPOLLHUP))
             && (revents & (EPOLLIN|EPOLLOUT)) == 0)
        {
            /*
             * if the error events were returned without EPOLLIN or EPOLLOUT,
             * then add these flags to handle the events at least in one
             * active handler
             */

            revents |= EPOLLIN|EPOLLOUT;
        }

		/*����Ƕ��¼���Ϊ��Ծ*/
        if ((revents & EPOLLIN) && rev->active) {

#if (NGX_HAVE_EPOLLRDHUP)
            if (revents & EPOLLRDHUP) {
                rev->pending_eof = 1;
            }
#endif

            rev->ready = 1;  //���¼�׼������

			/*
			 * NGX_POST_EVENTS��ʾ�Ӻ��������Ƿ����������¼����¼������������С�
			 * ������������¼�������뵽ngx_posted_accept_events��������뵽ngx_posted_events
			 * ��ngx_event_process_and_timer()�д�����ngx_posted_accept_events��ͻ��ͷŸ��ؾ�����
			 */
            if (flags & NGX_POST_EVENTS) {
                queue = rev->accept ? &ngx_posted_accept_events
                                    : &ngx_posted_events;

                ngx_post_event(rev, queue);

            } else {
                rev->handler(rev);  //�������ö��¼��Ļص�������������¼�
            }
        }

		//��ȡд�¼�
        wev = c->write;

        if ((revents & EPOLLOUT) && wev->active) {
			//���������
            if (c->fd == -1 || wev->instance != instance) {

                /*
                 * the stale event from a file descriptor
                 * that was just closed in this iteration
                 */

                ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                               "epoll: stale event %p", c);
                continue;
            }

            wev->ready = 1;  //д�¼�׼������
#if (NGX_THREADS)
            wev->complete = 1;
#endif
			//�Ӻ���д�¼����������������¼�
            if (flags & NGX_POST_EVENTS) {
                ngx_post_event(wev, &ngx_posted_events);

            } else {
                wev->handler(wev);
            }
        }
    }

    return NGX_OK;
}


#if (NGX_HAVE_FILE_AIO)

/*epoll_wait����ngx_eventfd_event�¼���ͻ������ص��÷��������Ѿ���ɵ��첽io�¼�*/
static void
ngx_epoll_eventfd_handler(ngx_event_t *ev)
{
    int               n, events;
    long              i;
    uint64_t          ready;
    ngx_err_t         err;
    ngx_event_t      *e;
    ngx_event_aio_t  *aio;
    struct io_event   event[64]; //һ������ദ��64���첽io�¼�
    struct timespec   ts;

    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ev->log, 0, "eventfd handler");

    /*ͨ��read��ȡ�Ѿ���ɵ��¼���Ŀ�������õ�ready�У�ע�������ready���Դ���64*/
    n = read(ngx_eventfd, &ready, 8);

    err = ngx_errno;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0, "eventfd: %d", n);

    if (n != 8) {
        if (n == -1) {
            if (err == NGX_EAGAIN) {
                return;
            }

            ngx_log_error(NGX_LOG_ALERT, ev->log, err, "read(eventfd) failed");
            return;
        }

        ngx_log_error(NGX_LOG_ALERT, ev->log, 0,
                      "read(eventfd) returned only %d bytes", n);
        return;
    }

    ts.tv_sec = 0;
    ts.tv_nsec = 0;

    while (ready) {

        /*������ɵ��첽io�����ж�ȡ����ɵ��¼�������ֵ�����ȡ���¼�����*/
        events = io_getevents(ngx_aio_ctx, 1, 64, event, &ts);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                       "io_getevents: %d", events);

        if (events > 0) {
            ready -= events;  //����ʣ������ɵ��첽io�¼�

            for (i = 0; i < events; i++) {

                ngx_log_debug4(NGX_LOG_DEBUG_EVENT, ev->log, 0,
                               "io_event: %XL %XL %L %L",
                                event[i].data, event[i].obj,
                                event[i].res, event[i].res2);

                /*data��Աָ������첽io�¼���Ӧ�ŵ�ʵ���¼�*/
                e = (ngx_event_t *) (uintptr_t) event[i].data;

                e->complete = 1;
                e->active = 0;  
                e->ready = 1;  //�¼��Ѿ�����

                aio = e->data;
                aio->res = event[i].res;

                ngx_post_event(e, &ngx_posted_events);  //���첽io�¼����뵽ngx_posted_events��ͨ��д�¼�������
            }

            continue;
        }

        if (events == 0) {
            return;
        }

        /* events == -1 */
        ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                      "io_getevents() failed");
        return;
    }
}

#endif

/*�������ڴ洢ngx_epoll_module����������Ľṹ��*/
static void *
ngx_epoll_create_conf(ngx_cycle_t *cycle)
{
    ngx_epoll_conf_t  *epcf;

    epcf = ngx_palloc(cycle->pool, sizeof(ngx_epoll_conf_t));
    if (epcf == NULL) {
        return NULL;
    }

    epcf->events = NGX_CONF_UNSET;
    epcf->aio_requests = NGX_CONF_UNSET;

    return epcf;
}

/*���������ļ���û�г��ֵ��������Ĭ��ֵ��ʼ�����Ӧ�Ľṹ���Ա*/
static char *
ngx_epoll_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_epoll_conf_t *epcf = conf;

    ngx_conf_init_uint_value(epcf->events, 512);
    ngx_conf_init_uint_value(epcf->aio_requests, 32);

    return NGX_CONF_OK;
}

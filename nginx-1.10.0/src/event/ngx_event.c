
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

/*
 *     �¼���������Ҫ���������������ռ�������ͷַ��¼���������˵���¼���Ҫ�������¼��Ͷ�ʱ���¼�Ϊ�����������¼���
 * ����tcp�����¼�Ϊ�����¼����������Ҫ�ڲ�ͬ�Ĳ���ϵͳ�ں���ѡ��һ���¼�����������֧�������¼��Ĵ���Nginx�������?
 *     ������������һ������ģ��ngx_events_module��������Nginx��������ʱ������ngx_init_cycle()�����������ļ���һ����
 * �����ļ��н�������ngx_events_moduleģ�����Ȥ��������"events {}"����ô���Ϳ�ʼ�����ˡ�������Ҫ��������Ϊ�����¼�ģ��
 * ����"events {}"����������������¼�ģ�������洢����������Ľṹ�壬����ngx_events_moduleģ��Ľӿ�ʵ�ֿ��Կ�������
 *     ��Σ�Nginx������һ���ǳ���Ҫ���¼�ģ�飬ngx_event_core_module�����ģ������ʹ�������¼��������ƣ��Լ���ι���
 * �¼���
 *     ���Nginx������һϵ���¼��������Ƶ�ʵ��ģ�飬��Linux����õľ���ngx_epoll_module.
 */

/*
 *     �¼��ǲ���Ҫ�����ģ���ΪNginx�������������Ѿ���ngx_cycle_t�ṹ���е�read_events��Ա��Ԥ���������еĶ��¼�������
 * write_events��Ա��Ԥ���������е�д�¼���ÿһ�����ӽ��Զ���Ӧһ�����¼���һ��д�¼���ֻҪ�����ӳ��л�ȡһ����������
 * �Ϳ����õ��¼��ˡ�
 *     NginxΪ���Ƿ�װ�����������������¼�������������Ӻ��Ƴ��¼���ngx_handle_read_event�����Ὣ���¼���ӵ��¼�����
 * ģ���У����������¼���Ӧ��tcp�����ϳ��ֿɶ��¼�ʱ�ͻ�ص����¼��Ĵ�������ngx_handle_write_event�����Ὣд�¼���ӵ�
 * �¼�����ģ���С�
 */

/*
 *     Nginx���ڳ�ַ��Ӷ��cpu�ܹ����ܵĿ��ǣ�ʹ���˶��worker�ӽ��̼�����ͬ�˿ڵ���ƣ��������worker�ӽ�����accept
 * ����������ʱ������������������������"��Ⱥ"���⡣�����ڽ�������ʱ�����漰�����ؾ�������⣬�ڶ��worker�ӽ���
 * ��������һ���������¼�ʱ��һ��ֻ��һ��worker�ӽ������ջ�ɹ��������ӣ��������һֱ�����������ֱ�����ӹرա�
 * ����������������Ľ���벻��Nginx��post���ơ����post���Ʊ�ʾ���������¼��Ӻ�ִ�С�Nginx���������post���У�һ��
 * ���ɱ������ļ������ӵĶ��¼����ɵ�ngx_posted_accept_events���У�һ��������ͨ��/д�¼����ɵ�ngx_posted_events����
 * post���Ƶľ��幦������:
 *     1.��epoll_wait������һ���¼����ֵ������������У��ô�����������¼���ngx_posted_accept_events��������ִ�У������
 * ��ͨ�¼���ngx_posted_events���к���ִ�С����ǽ�����ؾ����"��Ⱥ"�Ĺؼ���
 *     2.����ڴ���һ���¼��Ĺ����в�������һ���¼���������ϣ������¼����ִ��(��������ִ��)���Ϳ��Խ�����뵽post
 * �����С�
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


#define DEFAULT_CONNECTIONS  512


extern ngx_module_t ngx_kqueue_module;
extern ngx_module_t ngx_eventport_module;
extern ngx_module_t ngx_devpoll_module;
extern ngx_module_t ngx_epoll_module;
extern ngx_module_t ngx_select_module;


static char *ngx_event_init_conf(ngx_cycle_t *cycle, void *conf);
static ngx_int_t ngx_event_module_init(ngx_cycle_t *cycle);
static ngx_int_t ngx_event_process_init(ngx_cycle_t *cycle);
static char *ngx_events_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static char *ngx_event_connections(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static char *ngx_event_use(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_event_debug_connection(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

static void *ngx_event_core_create_conf(ngx_cycle_t *cycle);
static char *ngx_event_core_init_conf(ngx_cycle_t *cycle, void *conf);


static ngx_uint_t     ngx_timer_resolution;
sig_atomic_t          ngx_event_timer_alarm;

static ngx_uint_t     ngx_event_max_module;

ngx_uint_t            ngx_event_flags;
ngx_event_actions_t   ngx_event_actions;


static ngx_atomic_t   connection_counter = 1;
ngx_atomic_t         *ngx_connection_counter = &connection_counter;


ngx_atomic_t         *ngx_accept_mutex_ptr;  //ָ���ؾ�����
ngx_shmtx_t           ngx_accept_mutex;  //���ؾ�����
ngx_uint_t            ngx_use_accept_mutex;  //�������ؾ����־λ
ngx_uint_t            ngx_accept_events;
ngx_uint_t            ngx_accept_mutex_held;
ngx_msec_t            ngx_accept_mutex_delay;
ngx_int_t             ngx_accept_disabled;


#if (NGX_STAT_STUB)

ngx_atomic_t   ngx_stat_accepted0;
ngx_atomic_t  *ngx_stat_accepted = &ngx_stat_accepted0;
ngx_atomic_t   ngx_stat_handled0;
ngx_atomic_t  *ngx_stat_handled = &ngx_stat_handled0;
ngx_atomic_t   ngx_stat_requests0;
ngx_atomic_t  *ngx_stat_requests = &ngx_stat_requests0;
ngx_atomic_t   ngx_stat_active0;
ngx_atomic_t  *ngx_stat_active = &ngx_stat_active0;
ngx_atomic_t   ngx_stat_reading0;
ngx_atomic_t  *ngx_stat_reading = &ngx_stat_reading0;
ngx_atomic_t   ngx_stat_writing0;
ngx_atomic_t  *ngx_stat_writing = &ngx_stat_writing0;
ngx_atomic_t   ngx_stat_waiting0;
ngx_atomic_t  *ngx_stat_waiting = &ngx_stat_waiting0;

#endif

/*
 * ngx_events_moduleģ����һ������ģ�飬�书����:�������µ�ģ�������Լ�ÿ���¼�ģ�鶼��Ҫʵ�ֵ�ngx_event_module_t
 * �ӿڣ��Լ������¼�ģ�����ɵ����ڴ洢����������Ľṹ�壬������"events {}"�������������"events {}"������ʱ��
 * �������ngx_events_commands�ж���Ļص�����
 */

static ngx_command_t  ngx_events_commands[] = {

    { ngx_string("events"),
      NGX_MAIN_CONF|NGX_CONF_BLOCK|NGX_CONF_NOARGS,
      ngx_events_block,
      0,
      0,
      NULL },

      ngx_null_command
};

/*
 * ngx_events_moduleģ����һ������ģ�飬������Ҫʵ�ֺ���ģ��Ĺ�ͬ�ӿ�ngx_core_module_t��
 * ��Ϊngx_events_moduleֻ���ڳ���"events {}"����������ø����¼�ģ��ȥ����"events {}"�ڵ��������˱�����Ҫ
 * ʵ�����������洢����������Ľṹ��ķ���create_conf����ʵ����ʵ�ֵ�init_conf�ص�����ngx_event_init_confֻ���ж�
 * �¼�ģ���Ƿ����������ļ��н������ã����û���򷵻�ʧ�ܣ���Ϊ�¼�ģ���Ǳ����
 */
static ngx_core_module_t  ngx_events_module_ctx = {
    ngx_string("events"),
    NULL,
    ngx_event_init_conf
};

/*ngx_events_module�ӿڶ���*/
ngx_module_t  ngx_events_module = {
    NGX_MODULE_V1,
    &ngx_events_module_ctx,                /* module context */
    ngx_events_commands,                   /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

/*
 * ngx_event_core_moduleģ����һ���¼����͵�ģ�飬���������¼����͵�˳���ǵ�һλ�ģ���ͱ�֤�����������������¼�ģ��
 * ִ�У�ֻ�������������ѡ��ʹ�������¼���������
 */

static ngx_str_t  event_core_name = ngx_string("event_core");

/*ngx_event_core_moduleģ��֧�ֵ�����ָ��*/
static ngx_command_t  ngx_event_core_commands[] = {

    { ngx_string("worker_connections"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_event_connections,
      0,
      0,
      NULL },

    { ngx_string("use"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_event_use,
      0,
      0,
      NULL },

    { ngx_string("multi_accept"),
      NGX_EVENT_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_event_conf_t, multi_accept),
      NULL },

    { ngx_string("accept_mutex"),
      NGX_EVENT_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_event_conf_t, accept_mutex),
      NULL },

    { ngx_string("accept_mutex_delay"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      0,
      offsetof(ngx_event_conf_t, accept_mutex_delay),
      NULL },

    { ngx_string("debug_connection"),
      NGX_EVENT_CONF|NGX_CONF_TAKE1,
      ngx_event_debug_connection,
      0,
      0,
      NULL },

      ngx_null_command
};

/*ngx_event_core_moduleʵ�ֵ��¼�ģ��ͨ�ýӿ�*/
ngx_event_module_t  ngx_event_core_module_ctx = {
    &event_core_name,
    ngx_event_core_create_conf,            /* create configuration */
    ngx_event_core_init_conf,              /* init configuration */

     /*��Ϊ�������������¼�������������û��ʵ��actions*/
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
};

/*
 * ��Nginx���������л�û��fork��worker�ӽ��̵�ʱ�򣬻����ȵ���ngx_event_core_moduleģ���ngx_event_module_init������
 * ��fork��worker�ӽ��̺�ÿһ��worker���̻��ڵ���ngx_event_process_init���������ǽ��빤��ѭ��
 */
ngx_module_t  ngx_event_core_module = {
    NGX_MODULE_V1,
    &ngx_event_core_module_ctx,            /* module context */
    ngx_event_core_commands,               /* module directives */
    NGX_EVENT_MODULE,                      /* module type */
    NULL,                                  /* init master */
    ngx_event_module_init,                 /* init module */
    ngx_event_process_init,                /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

/*�¼��ռ��ͷַ��������*/
/*
 * ngx_process_events_and_timers()���¼��������Ƶĺ��ģ�����Ĳ�����Ҫ�����¼���:
 * 1.�����¼�����ģ��ʵ�ֵ�process_events()���������¼���
 * 2.��������post�¼������е��¼���
 * 3.���и��»���ʱ�����������ʱ���¼�
 */
void
ngx_process_events_and_timers(ngx_cycle_t *cycle)
{
    ngx_uint_t  flags;
    ngx_msec_t  timer, delta;

    /*
     *     ��������ļ���ʹ����timer_resolution��������Ӧ��ngx_timer_resolution����0��������Ҫ��ʱ�Ի���ʱ����и���
     * ִ�и��²�����ʱ��������ngx_timer_resolution����ʱ��timer��������Ϊ-1��������ngx_process_events()�������
     * ������,epoll_wait()��ΪtimerΪ-1���ڼ�ⲻ���¼���ʱ��Ҫ�ȴ���ֱ���Ѽ������Ѿ��������¼�Ȼ�󷵻ء������
     * ����������������õ���?
     *     �ں���ngx_event_process_init()�������⵽ngx_timer_resolution����0��������ϵͳ����settimer����һ����ʱ����
     * ��ʱʱ������ngx_timer_resolution���ȶ�ʱ����ʱ�����ó�ʱ�ص�����ngx_timer_signal_timer()������������лὫ
     * ����ʱ���־λngx_event_timer_alarm��1��Ȼ����ngx_process_events()�����⵽ngx_event_timer_alarmΪ1���͸���ʱ�䡣
     *     ����������ļ���û��ʹ��timer_resolution�������ô����else��֧�����ʱ������ôʵ�ָ��»���ʱ�����?
     *     ʵ����ͨ������ngx_event_find_timer()��������ȡ���뵱ǰ����ʱ������ĳ�ʱ�¼��뵱ǰ����ʱ��ļ������timer��
     * ����flags����ΪNGX_UPDATE_TIME,�����־λ������Ҫ���»���ʱ�䡣��ִ��ngx_process_events()ʱ����������������ȥ��
     * ��ngx_process_events()�У�epoll_wait()�����Ƿ��о����¼�����ȴ�timer���ŷ��أ���ô���ʱ��϶��г�ʱ�¼���
     * ��Ҫ���»���ʱ�䣬��ô��ʱ����flagsΪNGX_UPDATE_TIME�������ngx_timer_update()���»���ʱ��
     */
    if (ngx_timer_resolution) {
        timer = NGX_TIMER_INFINITE;
        flags = 0;

    } else {
        timer = ngx_event_find_timer();
        flags = NGX_UPDATE_TIME;

#if (NGX_WIN32)

        /* handle signals from master in case of network inactivity */

        if (timer == NGX_TIMER_INFINITE || timer > 500) {
            timer = 500;
        }

#endif
    }

    /*�������ؾ���*/
    if (ngx_use_accept_mutex) {
        if (ngx_accept_disabled > 0) {
            ngx_accept_disabled--;

        } else {
            /*
             * �ڴ򿪸��ؾ�����������£�ֻ�е���ngx_trylock_accept_mutex�����󣬻�ȡ���˸��ؾ�������
             * ��ǰ��worker�ӽ��̲Ż�ȥ����web�˿�
             */
            if (ngx_trylock_accept_mutex(cycle) == NGX_ERROR) {  //��ȡ���˸��ؾ�������ʹ�����м����˿����Ӷ��¼�
                return;
            }

            if (ngx_accept_mutex_held) {
                flags |= NGX_POST_EVENTS;  //�����Ӻ����¼������ⳤʱ��ռ��accpet_mutex

            } else {   //û�л�ȡ��������ngx_accept_mutex_delay����ٴγ��Ի�ȡ
                if (timer == NGX_TIMER_INFINITE
                    || timer > ngx_accept_mutex_delay)
                {
                    timer = ngx_accept_mutex_delay;
                }
            }
        }
    }

    delta = ngx_current_msec;

    /*�����epollģ�飬��ngx_process_eventsΪngx_epoll_process_events*/
    (void) ngx_process_events(cycle, timer, flags);

    delta = ngx_current_msec - delta;

    ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "timer delta: %M", delta);

    /*���ȴ���ngx_posted_accept_events���У�Ȼ���ͷŸ��ؾ�����*/
    ngx_event_process_posted(cycle, &ngx_posted_accept_events);

    if (ngx_accept_mutex_held) {
        ngx_shmtx_unlock(&ngx_accept_mutex);  //�����������Ӷ��У��ͷŸ��ؾ���������ʵ������Ϊ��һ��Ҫ�������ͷ�
    }

    if (delta) {  //delta���£�˵�������г�ʱ�¼�
        ngx_event_expire_timers();
    }

    /*����ngx_posted_events�����е���ͨ��д�¼�*/
    ngx_event_process_posted(cycle, &ngx_posted_events);
}

/* ��Ӧepoll��˵���ú����Ὣ��Ӧ���¼��Ա�Ե�����ķ�ʽ���뵽epoll�� */
ngx_int_t
ngx_handle_read_event(ngx_event_t *rev, ngx_uint_t flags)
{
    if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {

        /* kqueue, epoll */

        /* NGX_CLEAR_EVENT��ʾ��Ե���������Ҫ���뵽epoll�е�����¼��Ѿ���epoll�оͲ����ڼ����� */
        if (!rev->active && !rev->ready) {
            if (ngx_add_event(rev, NGX_READ_EVENT, NGX_CLEAR_EVENT)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }

        return NGX_OK;

    } else if (ngx_event_flags & NGX_USE_LEVEL_EVENT) {

        /* select, poll, /dev/poll */

        if (!rev->active && !rev->ready) {
            if (ngx_add_event(rev, NGX_READ_EVENT, NGX_LEVEL_EVENT)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (rev->active && (rev->ready || (flags & NGX_CLOSE_EVENT))) {
            if (ngx_del_event(rev, NGX_READ_EVENT, NGX_LEVEL_EVENT | flags)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

    } else if (ngx_event_flags & NGX_USE_EVENTPORT_EVENT) {

        /* event ports */

        if (!rev->active && !rev->ready) {
            if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (rev->oneshot && !rev->ready) {
            if (ngx_del_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }

            return NGX_OK;
        }
    }

    /* iocp */

    return NGX_OK;
}


ngx_int_t
ngx_handle_write_event(ngx_event_t *wev, size_t lowat)
{
    ngx_connection_t  *c;

    if (lowat) {
        c = wev->data;

        if (ngx_send_lowat(c, lowat) == NGX_ERROR) {
            return NGX_ERROR;
        }
    }

    if (ngx_event_flags & NGX_USE_CLEAR_EVENT) {

        /* kqueue, epoll */

        if (!wev->active && !wev->ready) {
            if (ngx_add_event(wev, NGX_WRITE_EVENT,
                              NGX_CLEAR_EVENT | (lowat ? NGX_LOWAT_EVENT : 0))
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }
        }

        return NGX_OK;

    } else if (ngx_event_flags & NGX_USE_LEVEL_EVENT) {

        /* select, poll, /dev/poll */

        if (!wev->active && !wev->ready) {
            if (ngx_add_event(wev, NGX_WRITE_EVENT, NGX_LEVEL_EVENT)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (wev->active && wev->ready) {
            if (ngx_del_event(wev, NGX_WRITE_EVENT, NGX_LEVEL_EVENT)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

    } else if (ngx_event_flags & NGX_USE_EVENTPORT_EVENT) {

        /* event ports */

        if (!wev->active && !wev->ready) {
            if (ngx_add_event(wev, NGX_WRITE_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }

            return NGX_OK;
        }

        if (wev->oneshot && wev->ready) {
            if (ngx_del_event(wev, NGX_WRITE_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }

            return NGX_OK;
        }
    }

    /* iocp */

    return NGX_OK;
}


static char *
ngx_event_init_conf(ngx_cycle_t *cycle, void *conf)
{
    /*�ж������ļ����Ƿ����¼�ģ����ص�������*/
    if (ngx_get_conf(cycle->conf_ctx, ngx_events_module) == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "no \"events\" section in configuration");
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

/*ngx_event_core_moduleģ��ʵ�ֵ�init_module�ص�����*/
static ngx_int_t
ngx_event_module_init(ngx_cycle_t *cycle)
{
    void              ***cf;
    u_char              *shared;
    size_t               size, cl;
    ngx_shm_t            shm;
    ngx_time_t          *tp;
    ngx_core_conf_t     *ccf;
    ngx_event_conf_t    *ecf;

    /*��ȡngx_event_core_moduleģ��Ĵ洢����������Ľṹ��ָ��*/
    cf = ngx_get_conf(cycle->conf_ctx, ngx_events_module);
    ecf = (*cf)[ngx_event_core_module.ctx_index];

    if (!ngx_test_config && ngx_process <= NGX_PROCESS_MASTER) {
        ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                      "using the \"%s\" event method", ecf->name);
    }

    /*��ȡ����ģ��ngx_core_module���ڴ洢����������Ľṹ��ָ��*/
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    ngx_timer_resolution = ccf->timer_resolution;  //���ø����ڴ�ʱ��ļ��

#if !(NGX_WIN32)
    {
    ngx_int_t      limit;
    struct rlimit  rlmt;

    if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "getrlimit(RLIMIT_NOFILE) failed, ignored");

    } else {
        if (ecf->connections > (ngx_uint_t) rlmt.rlim_cur
            && (ccf->rlimit_nofile == NGX_CONF_UNSET
                || ecf->connections > (ngx_uint_t) ccf->rlimit_nofile))
        {
            limit = (ccf->rlimit_nofile == NGX_CONF_UNSET) ?
                         (ngx_int_t) rlmt.rlim_cur : ccf->rlimit_nofile;

            ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                          "%ui worker_connections exceed "
                          "open file resource limit: %i",
                          ecf->connections, limit);
        }
    }
    }
#endif /* !(NGX_WIN32) */


    if (ccf->master == 0) {
        return NGX_OK;
    }

    if (ngx_accept_mutex_ptr) {
        return NGX_OK;
    }


    /* cl should be equal to or greater than cache line size */

    /*���빲���ڴ�*/
    cl = 128;

    size = cl            /* ngx_accept_mutex */
           + cl          /* ngx_connection_counter */
           + cl;         /* ngx_temp_number */

#if (NGX_STAT_STUB)

    size += cl           /* ngx_stat_accepted */
           + cl          /* ngx_stat_handled */
           + cl          /* ngx_stat_requests */
           + cl          /* ngx_stat_active */
           + cl          /* ngx_stat_reading */
           + cl          /* ngx_stat_writing */
           + cl;         /* ngx_stat_waiting */

#endif

    shm.size = size;
    shm.name.len = sizeof("nginx_shared_zone") - 1;
    shm.name.data = (u_char *) "nginx_shared_zone";
    shm.log = cycle->log;

    if (ngx_shm_alloc(&shm) != NGX_OK) {
        return NGX_ERROR;
    }

    /*sharedָ�����ڴ��׵�ַ*/
    shared = shm.addr;

    /*���ø��ؾ����������ַָ��*/
    ngx_accept_mutex_ptr = (ngx_atomic_t *) shared;
    ngx_accept_mutex.spin = (ngx_uint_t) -1;

    if (ngx_shmtx_create(&ngx_accept_mutex, (ngx_shmtx_sh_t *) shared,
                         cycle->lock_file.data)
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    ngx_connection_counter = (ngx_atomic_t *) (shared + 1 * cl);

    (void) ngx_atomic_cmp_set(ngx_connection_counter, 0, 1);

    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                   "counter: %p, %uA",
                   ngx_connection_counter, *ngx_connection_counter);

    ngx_temp_number = (ngx_atomic_t *) (shared + 2 * cl);

    tp = ngx_timeofday();

    ngx_random_number = (tp->msec << 16) + ngx_pid;

#if (NGX_STAT_STUB)

    ngx_stat_accepted = (ngx_atomic_t *) (shared + 3 * cl);
    ngx_stat_handled = (ngx_atomic_t *) (shared + 4 * cl);
    ngx_stat_requests = (ngx_atomic_t *) (shared + 5 * cl);
    ngx_stat_active = (ngx_atomic_t *) (shared + 6 * cl);
    ngx_stat_reading = (ngx_atomic_t *) (shared + 7 * cl);
    ngx_stat_writing = (ngx_atomic_t *) (shared + 8 * cl);
    ngx_stat_waiting = (ngx_atomic_t *) (shared + 9 * cl);

#endif

    return NGX_OK;
}


#if !(NGX_WIN32)
/*
 * ��ngx_event_actions_t�е�process_events�����У�ÿһ���¼�����ģ�鶼��Ҫ��ngx_event_timer_alarmΪ1��ʱ��
 * ����ngx_time_update���������»���ʱ�䣬���½�����ngx_event_tiemr_alram����
 */
static void
ngx_timer_signal_handler(int signo)
{
	//���ȫ�ֱ���Ϊ1����ʾҪ���»���ʱ��
    ngx_event_timer_alarm = 1;

#if 1
    ngx_log_debug0(NGX_LOG_DEBUG_EVENT, ngx_cycle->log, 0, "timer signal");
#endif
}

#endif

/*ngx_event_core_moduleģ��ʵ�ֵ�init process�ص�����,��ngx_worker_process_cycle()�е���*/
static ngx_int_t
ngx_event_process_init(ngx_cycle_t *cycle)
{
    ngx_uint_t           m, i;
    ngx_event_t         *rev, *wev;
    ngx_listening_t     *ls;
    ngx_connection_t    *c, *next, *old;
    ngx_core_conf_t     *ccf;
    ngx_event_conf_t    *ecf;
    ngx_event_module_t  *module;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    ecf = ngx_event_get_conf(cycle->conf_ctx, ngx_event_core_module);

    /*
     * �������ؾ������������(ȱһ����)
     * 1.masterģʽ
     * 2.worker�������̵���������1
     * 3.��accept_mutex���ؾ�����
     */
    if (ccf->master && ccf->worker_processes > 1 && ecf->accept_mutex) {
        ngx_use_accept_mutex = 1;  //�������ؾ���
        ngx_accept_mutex_held = 0;  //��ȡ���ؾ������ı�־λ
        ngx_accept_mutex_delay = ecf->accept_mutex_delay;  //�������λ�ȡaccept_mutex���ʱ��

    } else {
        ngx_use_accept_mutex = 0;  //�رո��ؾ���
    }

#if (NGX_WIN32)

    /*
     * disable accept mutex on win32 as it may cause deadlock if
     * grabbed by a process which can't accept connections
     */

    ngx_use_accept_mutex = 0;

#endif

    /*��ʼ�������¼�����*/
    ngx_queue_init(&ngx_posted_accept_events);
    ngx_queue_init(&ngx_posted_events);

    /*��ʱ����ʼ��������Ķ�ʱ����Ҫ������ά����ʱ���¼�*/
    if (ngx_event_timer_init(cycle->log) == NGX_ERROR) {
        return NGX_ERROR;
    }

    for (m = 0; cycle->modules[m]; m++) {
        if (cycle->modules[m]->type != NGX_EVENT_MODULE) {
            continue;
        }

		//�ҵ��¼�����ģ�飬��ngx_epoll_module
        if (cycle->modules[m]->ctx_index != ecf->use) {
            continue;
        }

        /*��ȡ�¼�ģ�����ʵ�ֵ�ngx_event_module_t�ӿ�*/
        module = cycle->modules[m]->ctx;

        /*��ʼ���¼�����ģ��*/
        if (module->actions.init(cycle, ngx_timer_resolution) != NGX_OK) {
            /* fatal */
            exit(2);
        }

        break;
    }

#if !(NGX_WIN32)
	/*������timer_resolution������Ҫ�����ڴ�ʱ����µľ��ȣ����Ҳ��Ƕ�ʱ���¼�*/
    if (ngx_timer_resolution && !(ngx_event_flags & NGX_USE_TIMER_EVENT)) {
        struct sigaction  sa;
        struct itimerval  itv;

        ngx_memzero(&sa, sizeof(struct sigaction));
        sa.sa_handler = ngx_timer_signal_handler;
        sigemptyset(&sa.sa_mask);

		/*ע��SIGALARM�������˸��źź���ûص�����������ź�*/
        if (sigaction(SIGALRM, &sa, NULL) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "sigaction(SIGALRM) failed");
            return NGX_ERROR;
        }

        itv.it_interval.tv_sec = ngx_timer_resolution / 1000;
        itv.it_interval.tv_usec = (ngx_timer_resolution % 1000) * 1000;
        itv.it_value.tv_sec = ngx_timer_resolution / 1000;
        itv.it_value.tv_usec = (ngx_timer_resolution % 1000 ) * 1000;

		/*������ʱ��������ʱ����Ϊngx_timer_resolution����ʱ������SIGALRM�źţ��ص�ngx_timer_signal_handler*/
        if (setitimer(ITIMER_REAL, &itv, NULL) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setitimer() failed");
        }
    }
	
	//���ʹ����poll����ģ�ͣ���Ԥ�ȷ������Ӿ��
    if (ngx_event_flags & NGX_USE_FD_EVENT) {
        struct rlimit  rlmt;

        if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "getrlimit(RLIMIT_NOFILE) failed");
            return NGX_ERROR;
        }

        cycle->files_n = (ngx_uint_t) rlmt.rlim_cur;

        cycle->files = ngx_calloc(sizeof(ngx_connection_t *) * cycle->files_n,
                                  cycle->log);
        if (cycle->files == NULL) {
            return NGX_ERROR;
        }
    }

#else

    if (ngx_timer_resolution && !(ngx_event_flags & NGX_USE_TIMER_EVENT)) {
        ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                      "the \"timer_resolution\" directive is not supported "
                      "with the configured event method, ignored");
        ngx_timer_resolution = 0;
    }

#endif

    /*Ԥ�������ӳ�*/
    cycle->connections =
        ngx_alloc(sizeof(ngx_connection_t) * cycle->connection_n, cycle->log);
    if (cycle->connections == NULL) {
        return NGX_ERROR;
    }

    c = cycle->connections;

    /*Ԥ������¼�*/
    cycle->read_events = ngx_alloc(sizeof(ngx_event_t) * cycle->connection_n,
                                   cycle->log);
    if (cycle->read_events == NULL) {
        return NGX_ERROR;
    }

    rev = cycle->read_events;
    for (i = 0; i < cycle->connection_n; i++) {
        rev[i].closed = 1;  //��ʼ״̬���������ӵĶ��¼����ǹرյģ��ҹ���
        rev[i].instance = 1;
    }

    /*Ԥ����д�¼�*/
    cycle->write_events = ngx_alloc(sizeof(ngx_event_t) * cycle->connection_n,
                                    cycle->log);
    if (cycle->write_events == NULL) {
        return NGX_ERROR;
    }

    wev = cycle->write_events;
    for (i = 0; i < cycle->connection_n; i++) {
        wev[i].closed = 1;  //��ʼ״̬���������ӵ�д�¼����ǹرյģ�
    }

    i = cycle->connection_n;
    next = NULL;

    /*����ngx_connection_t�ṹ���data��Ա������������������*/
    do {
        i--;

        c[i].data = next;  //����δʹ�õ����ӣ�data��Ա��Ϊ���ӳ������nextָ��
        c[i].read = &cycle->read_events[i];  //ÿ�����Ӷ���һ�����¼���һ��д�¼�����������й���
        c[i].write = &cycle->write_events[i];
        c[i].fd = (ngx_socket_t) -1;

        next = &c[i];
    } while (i);

    /*��ʼ״̬�����е����Ӷ��ǿ�������*/
    cycle->free_connections = next;
    cycle->free_connection_n = cycle->connection_n;

    /* for each listening socket */

    /*Ϊ���̵����м�������ngx_cycle_t->listening->connection��������*/
    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {

#if (NGX_HAVE_REUSEPORT)
        if (ls[i].reuseport && ls[i].worker != ngx_worker) {
            continue;
        }
#endif

        /*��ȡ���ӳ�*/
        c = ngx_get_connection(ls[i].fd, cycle->log);

        if (c == NULL) {
            return NGX_ERROR;
        }

        c->type = ls[i].type;
        c->log = &ls[i].log;

        c->listening = &ls[i];
        ls[i].connection = c;

        /*��ȡ���ӵĶ��¼�*/
        rev = c->read;

        rev->log = c->log;
        /*
         * acceptΪ1��������Ϊ���¼���������,ֻ�з���˼���socket�����Ӷ�����¼��ĸñ�־λ����1,
         * �ͻ��˺ͷ���˽���ʹ�õ�socket��Ӧ��ngx_connection_t����Ķ�д�¼��ñ�־λ��������
         * ��Ϊֻ�м���socket�յ��ͻ��˷����Ľ������ĺ�ŻὨ�����ͻ��˺ͷ���˽�����socket���Ѿ������ġ�
         */
        rev->accept = 1;

#if (NGX_HAVE_DEFERRED_ACCEPT)
        rev->deferred_accept = ls[i].deferred_accept;
#endif

        if (!(ngx_event_flags & NGX_USE_IOCP_EVENT)) {
            if (ls[i].previous) {

                /*
                 * delete the old accept events that were bound to
                 * the old cycle read events array
                 */

                old = ls[i].previous->connection;

                if (ngx_del_event(old->read, NGX_READ_EVENT, NGX_CLOSE_EVENT)
                    == NGX_ERROR)
                {
                    return NGX_ERROR;
                }

                old->fd = (ngx_socket_t) -1;
            }
        }

#if (NGX_WIN32)

        if (ngx_event_flags & NGX_USE_IOCP_EVENT) {
            ngx_iocp_conf_t  *iocpcf;

            rev->handler = ngx_event_acceptex;

            if (ngx_use_accept_mutex) {
                continue;
            }

            if (ngx_add_event(rev, 0, NGX_IOCP_ACCEPT) == NGX_ERROR) {
                return NGX_ERROR;
            }

            ls[i].log.handler = ngx_acceptex_log_error;

            iocpcf = ngx_event_get_conf(cycle->conf_ctx, ngx_iocp_module);
            if (ngx_event_post_acceptex(&ls[i], iocpcf->post_acceptex)
                == NGX_ERROR)
            {
                return NGX_ERROR;
            }

        } else {
            rev->handler = ngx_event_accept;

            if (ngx_use_accept_mutex) {
                continue;
            }

            if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

#else

        /*�������˿ڵĶ��¼�����Ϊngx_event_accept,�����µ������¼�ʱ������ngx_event_accept��������*/
        rev->handler = (c->type == SOCK_STREAM) ? ngx_event_accept
                                                : ngx_event_recvmsg;

        /*�������ngx_use_accept_mutex������ʱ���Ὣ���¼���ӵ��¼�����ģ����*/
        /* 
         * ʹ���˸��ؾ��⣬��ʱ���������׽��ַ���epoll��, ���ǵȵ�worker����ngx_accept_mutex��������ٷ���epoll��
         * ���⾪Ⱥ�ķ�������ngx_process_events_and_timers->ngx_trylock_accept_mutex 
         */
        if (ngx_use_accept_mutex
#if (NGX_HAVE_REUSEPORT)
            && !ls[i].reuseport
#endif
           )
        {
            continue;
        }

        /*���������ӵĶ��¼���ӵ��¼�����ģ����*/
        /*
         * �����׽ӿڶ����ngx_listening_�ļ���socket��Ӧ�����ӵĶ��¼�����ˮƽ������ʽ���뵽epoll
         * �еģ���Ϊ��Ĭ������£�epoll����ĳ����������˵Ĭ������ˮƽ������ʽ�����ģ�������ʾ
         * ָ���Ǳ�Ե������ʽ�����������ngx_add_event(rev, NGX_READ_EVENT, 0)����ˮƽ��ʽ�ġ�
         * ���ӽ���֮��Ŀͻ��������˽�����socket��Ӧ�����ӵĶ�д�¼������Ա�Ե������ʽ���뵽epoll
         * �еģ������ngx_event_accept()�е�ngx_add_conn()����
         */
        if (ngx_add_event(rev, NGX_READ_EVENT, 0) == NGX_ERROR) {
            return NGX_ERROR;
        }

#endif

    }

    return NGX_OK;
}


ngx_int_t
ngx_send_lowat(ngx_connection_t *c, size_t lowat)
{
    int  sndlowat;

#if (NGX_HAVE_LOWAT_EVENT)

    if (ngx_event_flags & NGX_USE_KQUEUE_EVENT) {
        c->write->available = lowat;
        return NGX_OK;
    }

#endif

    if (lowat == 0 || c->sndlowat) {
        return NGX_OK;
    }

    sndlowat = (int) lowat;

    if (setsockopt(c->fd, SOL_SOCKET, SO_SNDLOWAT,
                   (const void *) &sndlowat, sizeof(int))
        == -1)
    {
        ngx_connection_error(c, ngx_socket_errno,
                             "setsockopt(SO_SNDLOWAT) failed");
        return NGX_ERROR;
    }

    c->sndlowat = 1;

    return NGX_OK;
}

/*
 *     ÿһ���¼�ģ�鶼��Ҫʵ��ngx_event_module_t�ӿڣ�����ӿ�������ÿ���¼�ģ�齨���Լ������ڴ洢����������Ľṹ��
 * ���ڴ洢�������ļ��н����õ��Ķ�Ӧ���������¼�����ģ��ngx_events_module����ι�����Щ�¼�ģ�����ڴ洢���������
 * �Ľṹ�����?
 *     ��ʵ��ÿ���¼�ģ����������ڴ洢����������Ľṹ�嶼�ᱻ���õ�ngx_events_moduleģ�鴴����ָ�������У�ngx_events_module
 * ģ�鴴�������ָ������ᱻ������������?
 *     �ں��Ľṹ��ngx_cycle_t�У���һ����Աconf_ctx,��ָ��һ��ָ�����飬�����ָ������(�±���ģ��index)���δ��������
 * ����ģ�������������ָ�롣��Ĭ�ϵı���˳���£���ngx_modules.c�ļ��п��Կ���ngx_events_moduleģ����
 * ngx_modules����ĵ�����λ�á���ˣ�conf_ctx�ĵ�����ָ��ͱ�����ngx_events_moduleģ�鴴�������ڴ洢�����¼�ģ��
 * �����������ָ������ĵ�ַ��
 */

/*�������ļ��г���"events {}"������ʱ�������ngx_events_block�������������������¼�ģ������������Ȥ��������*/
static char *
ngx_events_block(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    char                 *rv;
    void               ***ctx;
    ngx_uint_t            i;
    ngx_conf_t            pcf;
    ngx_event_module_t   *m;

    if (*(void **) conf) {
        return "is duplicate";
    }

    /* count the number of the event modules and set up their indices */

	//��ȡ�¼�ģ��ĸ���������ʼ�������¼�ģ����ͬ��ģ���е�ctx_index
    ngx_event_max_module = ngx_count_modules(cf->cycle, NGX_EVENT_MODULE);

    ctx = ngx_pcalloc(cf->pool, sizeof(void *));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

	//�������ڴ洢�����¼�ģ�������������Ľṹ��
    *ctx = ngx_pcalloc(cf->pool, ngx_event_max_module * sizeof(void *));
    if (*ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    /*
     * conf��ʱ����ngx_cycle_t->conf_ctx[5]����Ϊ������ģ�����ngx_events_module�����������ｫconfָ��洢������
     * �¼�ģ�������洢����������Ľṹ�幹�ɵ�ָ�����飬������ָ����ngx_events_moduleģ��ά��.
     */
    *(void **) conf = ctx;

    for (i = 0; cf->cycle->modules[i]; i++) {
        if (cf->cycle->modules[i]->type != NGX_EVENT_MODULE) {
            continue;
        }

        m = cf->cycle->modules[i]->ctx;
		//���������¼�ģ���create_conf�ص����������洢����������Ľṹ��
        if (m->create_conf) {
            (*ctx)[cf->cycle->modules[i]->ctx_index] =
                                                     m->create_conf(cf->cycle);
            if ((*ctx)[cf->cycle->modules[i]->ctx_index] == NULL) {
                return NGX_CONF_ERROR;
            }
        }
    }

    pcf = *cf;
    cf->ctx = ctx;
    cf->module_type = NGX_EVENT_MODULE;
    cf->cmd_type = NGX_EVENT_CONF;

	//����������,���������洢����Ӧ�Ľṹ���Ա�У�
	//��������Ӧ������ʱ�����ngx_command_t��ʵ�ֵ�set���Ӻ�����ȡ����ֵ
    rv = ngx_conf_parse(cf, NULL);

    *cf = pcf;

    if (rv != NGX_CONF_OK) {
        return rv;
    }

    for (i = 0; cf->cycle->modules[i]; i++) {
        if (cf->cycle->modules[i]->type != NGX_EVENT_MODULE) {
            continue;
        }

        m = cf->cycle->modules[i]->ctx;
		//���������¼�ģ���init_conf�ص�������ʼ���ṹ��
        if (m->init_conf) {
            rv = m->init_conf(cf->cycle,
                              (*ctx)[cf->cycle->modules[i]->ctx_index]);
            if (rv != NGX_CONF_OK) {
                return rv;
            }
        }
    }

    return NGX_CONF_OK;
}


static char *
ngx_event_connections(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_event_conf_t  *ecf = conf;

    ngx_str_t  *value;

    if (ecf->connections != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

	//���е����������ֵ�����cf->args��
    value = cf->args->elts;
    ecf->connections = ngx_atoi(value[1].data, value[1].len);
    if (ecf->connections == (ngx_uint_t) NGX_ERROR) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid number \"%V\"", &value[1]);

        return NGX_CONF_ERROR;
    }

	//����cycle�е�ǰ���̵����������
    cf->cycle->connection_n = ecf->connections;

    return NGX_CONF_OK;
}

/*��ȡ��ʹ�õ��¼��������ͣ����洢�ڽṹ����Ӧ��Ա��*/
static char *
ngx_event_use(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_event_conf_t  *ecf = conf;

    ngx_int_t             m;
    ngx_str_t            *value;
    ngx_event_conf_t     *old_ecf;
    ngx_event_module_t   *module;

    if (ecf->use != NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (cf->cycle->old_cycle->conf_ctx) {
        old_ecf = ngx_event_get_conf(cf->cycle->old_cycle->conf_ctx,
                                     ngx_event_core_module);
    } else {
        old_ecf = NULL;
    }


    for (m = 0; cf->cycle->modules[m]; m++) {
        if (cf->cycle->modules[m]->type != NGX_EVENT_MODULE) {
            continue;
        }

        module = cf->cycle->modules[m]->ctx;
        if (module->name->len == value[1].len) {
            if (ngx_strcmp(module->name->data, value[1].data) == 0) {
                ecf->use = cf->cycle->modules[m]->ctx_index;
                ecf->name = module->name->data;

                if (ngx_process == NGX_PROCESS_SINGLE
                    && old_ecf
                    && old_ecf->use != ecf->use)
                {
                    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "when the server runs without a master process "
                               "the \"%V\" event type must be the same as "
                               "in previous configuration - \"%s\" "
                               "and it cannot be changed on the fly, "
                               "to change it you need to stop server "
                               "and start it again",
                               &value[1], old_ecf->name);

                    return NGX_CONF_ERROR;
                }

                return NGX_CONF_OK;
            }
        }
    }

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "invalid event type \"%V\"", &value[1]);

    return NGX_CONF_ERROR;
}


static char *
ngx_event_debug_connection(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_DEBUG)
    ngx_event_conf_t  *ecf = conf;

    ngx_int_t             rc;
    ngx_str_t            *value;
    ngx_url_t             u;
    ngx_cidr_t            c, *cidr;
    ngx_uint_t            i;
    struct sockaddr_in   *sin;
#if (NGX_HAVE_INET6)
    struct sockaddr_in6  *sin6;
#endif

    value = cf->args->elts;

#if (NGX_HAVE_UNIX_DOMAIN)

    if (ngx_strcmp(value[1].data, "unix:") == 0) {
        cidr = ngx_array_push(&ecf->debug_connection);
        if (cidr == NULL) {
            return NGX_CONF_ERROR;
        }

        cidr->family = AF_UNIX;
        return NGX_CONF_OK;
    }

#endif

    rc = ngx_ptocidr(&value[1], &c);

    if (rc != NGX_ERROR) {
        if (rc == NGX_DONE) {
            ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                               "low address bits of %V are meaningless",
                               &value[1]);
        }

        cidr = ngx_array_push(&ecf->debug_connection);
        if (cidr == NULL) {
            return NGX_CONF_ERROR;
        }

        *cidr = c;

        return NGX_CONF_OK;
    }

    ngx_memzero(&u, sizeof(ngx_url_t));
    u.host = value[1];

    if (ngx_inet_resolve_host(cf->pool, &u) != NGX_OK) {
        if (u.err) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "%s in debug_connection \"%V\"",
                               u.err, &u.host);
        }

        return NGX_CONF_ERROR;
    }

    cidr = ngx_array_push_n(&ecf->debug_connection, u.naddrs);
    if (cidr == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memzero(cidr, u.naddrs * sizeof(ngx_cidr_t));

    for (i = 0; i < u.naddrs; i++) {
        cidr[i].family = u.addrs[i].sockaddr->sa_family;

        switch (cidr[i].family) {

#if (NGX_HAVE_INET6)
        case AF_INET6:
            sin6 = (struct sockaddr_in6 *) u.addrs[i].sockaddr;
            cidr[i].u.in6.addr = sin6->sin6_addr;
            ngx_memset(cidr[i].u.in6.mask.s6_addr, 0xff, 16);
            break;
#endif

        default: /* AF_INET */
            sin = (struct sockaddr_in *) u.addrs[i].sockaddr;
            cidr[i].u.in.addr = sin->sin_addr.s_addr;
            cidr[i].u.in.mask = 0xffffffff;
            break;
        }
    }

#else

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"debug_connection\" is ignored, you need to rebuild "
                       "nginx using --with-debug option to enable it");

#endif

    return NGX_CONF_OK;
}


static void *
ngx_event_core_create_conf(ngx_cycle_t *cycle)
{
    ngx_event_conf_t  *ecf;

    ecf = ngx_palloc(cycle->pool, sizeof(ngx_event_conf_t));
    if (ecf == NULL) {
        return NULL;
    }

    ecf->connections = NGX_CONF_UNSET_UINT;
    ecf->use = NGX_CONF_UNSET_UINT;
    ecf->multi_accept = NGX_CONF_UNSET;
    ecf->accept_mutex = NGX_CONF_UNSET;
    ecf->accept_mutex_delay = NGX_CONF_UNSET_MSEC;
    ecf->name = (void *) NGX_CONF_UNSET;

#if (NGX_DEBUG)

    if (ngx_array_init(&ecf->debug_connection, cycle->pool, 4,
                       sizeof(ngx_cidr_t)) == NGX_ERROR)
    {
        return NULL;
    }

#endif

    return ecf;
}


static char *
ngx_event_core_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_event_conf_t  *ecf = conf;

#if (NGX_HAVE_EPOLL) && !(NGX_TEST_BUILD_EPOLL)
    int                  fd;
#endif
    ngx_int_t            i;
    ngx_module_t        *module;
    ngx_event_module_t  *event_module;

    module = NULL;

#if (NGX_HAVE_EPOLL) && !(NGX_TEST_BUILD_EPOLL)

    fd = epoll_create(100);

    if (fd != -1) {
        (void) close(fd);
        module = &ngx_epoll_module;

    } else if (ngx_errno != NGX_ENOSYS) {
        module = &ngx_epoll_module;
    }

#endif

#if (NGX_HAVE_DEVPOLL) && !(NGX_TEST_BUILD_DEVPOLL)

    module = &ngx_devpoll_module;

#endif

#if (NGX_HAVE_KQUEUE)

    module = &ngx_kqueue_module;

#endif

#if (NGX_HAVE_SELECT)

    if (module == NULL) {
        module = &ngx_select_module;
    }

#endif

    if (module == NULL) {
        for (i = 0; cycle->modules[i]; i++) {

            if (cycle->modules[i]->type != NGX_EVENT_MODULE) {
                continue;
            }

            event_module = cycle->modules[i]->ctx;

            if (ngx_strcmp(event_module->name->data, event_core_name.data) == 0)
            {
                continue;
            }

            module = cycle->modules[i];
            break;
        }
    }

    if (module == NULL) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0, "no events module found");
        return NGX_CONF_ERROR;
    }

    ngx_conf_init_uint_value(ecf->connections, DEFAULT_CONNECTIONS);
    cycle->connection_n = ecf->connections;

    ngx_conf_init_uint_value(ecf->use, module->ctx_index);

    event_module = module->ctx;
    ngx_conf_init_ptr_value(ecf->name, event_module->name->data);

    ngx_conf_init_value(ecf->multi_accept, 0);
    ngx_conf_init_value(ecf->accept_mutex, 1);
    ngx_conf_init_msec_value(ecf->accept_mutex_delay, 500);

    return NGX_CONF_OK;
}

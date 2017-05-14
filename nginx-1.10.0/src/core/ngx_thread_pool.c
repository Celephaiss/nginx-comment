
/*
 * Copyright (C) Nginx, Inc.
 * Copyright (C) Valentin V. Bartenev
 * Copyright (C) Ruslan Ermilov
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_thread_pool.h>


/* 
 * ���ڴ�������ļ������õ��̳߳���Ϣ��pools��̬����ÿ��Ԫ�ض���һ���̳߳����ö���
 * ����Ϊngx_thread_pool_t���ο�ngx_thread_pool_add()
 */
typedef struct {
    ngx_array_t               pools;
} ngx_thread_pool_conf_t;

/* �̳߳�������� */
typedef struct {
    ngx_thread_task_t        *first;
    ngx_thread_task_t       **last;
} ngx_thread_pool_queue_t;

#define ngx_thread_pool_queue_init(q)                                         \
    (q)->first = NULL;                                                        \
    (q)->last = &(q)->first

/* �̳߳ض������� */
struct ngx_thread_pool_s {
    ngx_thread_mutex_t        mtx;
    ngx_thread_pool_queue_t   queue;  /* ����ȴ����� */
    ngx_int_t                 waiting;  /* ��ǰ����ȴ������е�������� */
    ngx_thread_cond_t         cond;

    ngx_log_t                *log;

    ngx_str_t                 name;  /* �̳߳����� */
    ngx_uint_t                threads;  /* �̳߳����̵߳����� */
    /* 
     * �ȴ�����������������������̳߳��������̶߳�����busy״̬ʱ��
     * ����ͻᱻ���浽�����У�����������ˣ�����ͻ᷵�ش���
     */
    ngx_int_t                 max_queue;

    /* �����ļ������� */
    u_char                   *file;

    /* thread_pool�����������ļ��е��к� */
    ngx_uint_t                line;
};


static ngx_int_t ngx_thread_pool_init(ngx_thread_pool_t *tp, ngx_log_t *log,
    ngx_pool_t *pool);
static void ngx_thread_pool_destroy(ngx_thread_pool_t *tp);
static void ngx_thread_pool_exit_handler(void *data, ngx_log_t *log);

static void *ngx_thread_pool_cycle(void *data);
static void ngx_thread_pool_handler(ngx_event_t *ev);

static char *ngx_thread_pool(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

static void *ngx_thread_pool_create_conf(ngx_cycle_t *cycle);
static char *ngx_thread_pool_init_conf(ngx_cycle_t *cycle, void *conf);

static ngx_int_t ngx_thread_pool_init_worker(ngx_cycle_t *cycle);
static void ngx_thread_pool_exit_worker(ngx_cycle_t *cycle);

/* ngx_thread_pool_moduleģ��֧�ֵ��������� */
static ngx_command_t  ngx_thread_pool_commands[] = {

    { ngx_string("thread_pool"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE23,
      ngx_thread_pool,
      0,
      0,
      NULL },

      ngx_null_command
};

/* ngx_thread_pool_moduleģ�����������Ϣ */
static ngx_core_module_t  ngx_thread_pool_module_ctx = {
    ngx_string("thread_pool"),
    ngx_thread_pool_create_conf,
    ngx_thread_pool_init_conf
};


ngx_module_t  ngx_thread_pool_module = {
    NGX_MODULE_V1,
    &ngx_thread_pool_module_ctx,           /* module context */
    ngx_thread_pool_commands,              /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    ngx_thread_pool_init_worker,           /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    ngx_thread_pool_exit_worker,           /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};

/* �̳߳�Ĭ�����֣���������ļ���û���������ֵģ����������Ĭ�ϵ����� */
static ngx_str_t  ngx_thread_pool_default = ngx_string("default");

/* ����id */
static ngx_uint_t               ngx_thread_pool_task_id;
static ngx_atomic_t             ngx_thread_pool_done_lock;
/* ���ڴ�Ŵ����������(������task->handler()�ص�)�Ķ��� */
static ngx_thread_pool_queue_t  ngx_thread_pool_done;

/* �̳߳س�ʼ�� */
static ngx_int_t
ngx_thread_pool_init(ngx_thread_pool_t *tp, ngx_log_t *log, ngx_pool_t *pool)
{
    int             err;
    pthread_t       tid;
    ngx_uint_t      n;
    pthread_attr_t  attr;

    if (ngx_notify == NULL) {
        ngx_log_error(NGX_LOG_ALERT, log, 0,
               "the configured event method cannot be used with thread pools");
        return NGX_ERROR;
    }

    /* ��ʼ���߳��е�������� */
    ngx_thread_pool_queue_init(&tp->queue);

    /* ����pthread_mutex_t */
    if (ngx_thread_mutex_create(&tp->mtx, log) != NGX_OK) {
        return NGX_ERROR;
    }

    /* ����pthread_cond_t */
    if (ngx_thread_cond_create(&tp->cond, log) != NGX_OK) {
        (void) ngx_thread_mutex_destroy(&tp->mtx, log);
        return NGX_ERROR;
    }

    tp->log = log;

    err = pthread_attr_init(&attr);
    if (err) {
        ngx_log_error(NGX_LOG_ALERT, log, err,
                      "pthread_attr_init() failed");
        return NGX_ERROR;
    }

#if 0
    err = pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN);
    if (err) {
        ngx_log_error(NGX_LOG_ALERT, log, err,
                      "pthread_attr_setstacksize() failed");
        return NGX_ERROR;
    }
#endif

    /* �������õ��߳�������������Ӧ�������߳� */
    for (n = 0; n < tp->threads; n++) {
        err = pthread_create(&tid, &attr, ngx_thread_pool_cycle, tp);
        if (err) {
            ngx_log_error(NGX_LOG_ALERT, log, err,
                          "pthread_create() failed");
            return NGX_ERROR;
        }
    }

    (void) pthread_attr_destroy(&attr);

    return NGX_OK;
}

/* �����̳߳� */
static void
ngx_thread_pool_destroy(ngx_thread_pool_t *tp)
{
    ngx_uint_t           n;
    ngx_thread_task_t    task;
    volatile ngx_uint_t  lock;

    ngx_memzero(&task, sizeof(ngx_thread_task_t));

    /* ����һ���߳��������� */
    task.handler = ngx_thread_pool_exit_handler;
    task.ctx = (void *) &lock;

    /* �����̳߳��е�ÿһ���̣߳�����һ���߳���ֹ���� */
    for (n = 0; n < tp->threads; n++) {
        lock = 1;

        /* ����ַ�����������뵽tp->queue�����У������̳߳��е��̻߳�Ӷ�����ȡ������������ */
        if (ngx_thread_task_post(tp, &task) != NGX_OK) {
            return;
        }

        /* �ȴ� */
        while (lock) {
            ngx_sched_yield();
        }

        task.event.active = 0;
    }

    /* ����pthread_cond_t��pthread_mutex_t���� */
    (void) ngx_thread_cond_destroy(&tp->cond, tp->log)
    (void) ngx_thread_mutex_destroy(&tp->mtx, tp->log);
}

/* �̵߳���pthread_exit()��������ֹ */
static void
ngx_thread_pool_exit_handler(void *data, ngx_log_t *log)
{
    ngx_uint_t *lock = data;

    *lock = 0;

    pthread_exit(0);
}


/* ����һ���߳����� */
ngx_thread_task_t *
ngx_thread_task_alloc(ngx_pool_t *pool, size_t size)
{
    ngx_thread_task_t  *task;

    task = ngx_pcalloc(pool, sizeof(ngx_thread_task_t) + size);
    if (task == NULL) {
        return NULL;
    }

    /* 
     * ��Ϊtask����Ϊngx_thread_task_t������task + 1֮��ĵ�ַ��ָ����
     * size���ֶ�Ӧ���ڴ���ʼ��ַ��������ƫ����sizeof(ngx_thread_task_t)��С��
     * �ⲿ���ڴ���������Ҫ�õ��̳߳ص�ģ���������˽�����ݵġ��ⲿ�ֿ��Բο�
     * ����ngx_thread_write_chain_to_file()
     */
    task->ctx = task + 1;

    return task;
}

/* ���̷ַ߳����񣬽�������ӵ�tp->queue�����У������ѹ�����߳� */
ngx_int_t
ngx_thread_task_post(ngx_thread_pool_t *tp, ngx_thread_task_t *task)
{
    /* ��������Ӧ���¼��ǻ�Ծ�ģ������ٴηַ������� */
    if (task->event.active) {
        ngx_log_error(NGX_LOG_ALERT, tp->log, 0,
                      "task #%ui already active", task->id);
        return NGX_ERROR;
    }

    /* ���Ի�ȡ������tp->mtx */
    if (ngx_thread_mutex_lock(&tp->mtx, tp->log) != NGX_OK) {
        return NGX_ERROR;
    }

    /* 
     * ����ȴ����������������Ѿ�������max_queue���򷵻�ʧ�ܣ����ֻ�ܻ���
     * tp->max_queue��������
     */
    if (tp->waiting >= tp->max_queue) {
        (void) ngx_thread_mutex_unlock(&tp->mtx, tp->log);

        ngx_log_error(NGX_LOG_ERR, tp->log, 0,
                      "thread pool \"%V\" queue overflow: %i tasks waiting",
                      &tp->name, tp->waiting);
        return NGX_ERROR;
    }

    /* �������Ӧ���¼�����Ϊ��Ծ */
    task->event.active = 1;

    task->id = ngx_thread_pool_task_id++;
    task->next = NULL;

    /* 
     *     pthread_cond_signal�����������Ƿ���һ���źŸ�һ�����ڴ���
     * �����ȴ�״̬���߳�,ʹ����������״̬,����ִ��.���û���̴߳���
     * �����ȴ�״̬,pthread_cond_signalҲ��ɹ����ء���tp->queue������
     * û�������ʱ���̳߳��е��߳̾ͻ���𣬵ȴ�����ַ��󱻻��ѣ�
     * �ⲿ�ֿ��Բο�ngx_thread_pool_cycle()����̴߳�������
     *     ʹ��pthread_cond_signal�����С���Ⱥ���󡱲����������ֻ��һ���߳�
     * ���źš������ж���߳����������ȴ���������������Ļ�����ô�Ǹ��ݸ��ȴ�
     * �߳����ȼ��ĸߵ�ȷ���ĸ��߳̽��յ��źſ�ʼ����ִ�С�������߳����ȼ���ͬ��
     * ����ݵȴ�ʱ��ĳ�����ȷ���ĸ��̻߳���źš����������һ��pthread_cond_signal
     * ������෢�ź�һ�Ρ�
     *     pthread_cond_wait�������pthread_mutex_lock��pthread_mutex_unlock֮�䣬
     * ��Ϊ��Ҫ���ݹ��������״̬�������Ƿ�Ҫ�ȴ�����Ϊ�˲���Զ�ȴ���ȥ��
     * ���Ա���Ҫ��lock/unlock����
     */
    if (ngx_thread_cond_signal(&tp->cond, tp->log) != NGX_OK) {
        (void) ngx_thread_mutex_unlock(&tp->mtx, tp->log);
        return NGX_ERROR;
    }

    /* ��������뵽�̳߳صȴ������� */
    *tp->queue.last = task;
    tp->queue.last = &task->next;

    /* �ۼӵȴ�����ĸ���ͳ�� */
    tp->waiting++;

    (void) ngx_thread_mutex_unlock(&tp->mtx, tp->log);

    ngx_log_debug2(NGX_LOG_DEBUG_CORE, tp->log, 0,
                   "task #%ui added to thread pool \"%V\"",
                   task->id, &tp->name);

    return NGX_OK;
}

/* �̴߳����� */
static void *
ngx_thread_pool_cycle(void *data)
{
    ngx_thread_pool_t *tp = data;

    int                 err;
    sigset_t            set;
    ngx_thread_task_t  *task;

#if 0
    ngx_time_update();
#endif

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, tp->log, 0,
                   "thread in pool \"%V\" started", &tp->name);

    sigfillset(&set);

    sigdelset(&set, SIGILL);
    sigdelset(&set, SIGFPE);
    sigdelset(&set, SIGSEGV);
    sigdelset(&set, SIGBUS);

    err = pthread_sigmask(SIG_BLOCK, &set, NULL);
    if (err) {
        ngx_log_error(NGX_LOG_ALERT, tp->log, err, "pthread_sigmask() failed");
        return NULL;
    }

    for ( ;; ) {
        if (ngx_thread_mutex_lock(&tp->mtx, tp->log) != NGX_OK) {
            return NULL;
        }

        /* the number may become negative */
        /* 
         * ÿ��ѭ������һ���������Զ������������һ�����ȴ����������һ����
         * ����Ϊʲô������������Ƿ�������������֮����?
         */
        tp->waiting--;

        /* 
         * ���tp->queue����Ϊ�գ����ȹ���ȴ��������ַ�����֮��ᱻ���ѣ�
         * �ַ�������ѹ����߳��ⲿ�ִ�����Բο�ngx_thread_task_post()����
         */
        while (tp->queue.first == NULL) {
            if (ngx_thread_cond_wait(&tp->cond, &tp->mtx, tp->log)
                != NGX_OK)
            {
                (void) ngx_thread_mutex_unlock(&tp->mtx, tp->log);
                return NULL;
            }
        }

        /* ������ȴ�������ȡ��һ������ */
        task = tp->queue.first;
        tp->queue.first = task->next;

        if (tp->queue.first == NULL) {
            tp->queue.last = &tp->queue.first;
        }

        if (ngx_thread_mutex_unlock(&tp->mtx, tp->log) != NGX_OK) {
            return NULL;
        }

#if 0
        ngx_time_update();
#endif

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, tp->log, 0,
                       "run task #%ui in thread pool \"%V\"",
                       task->id, &tp->name);

        /* ��������Ĵ����� */
        task->handler(task->ctx, tp->log);

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, tp->log, 0,
                       "complete task #%ui in thread pool \"%V\"",
                       task->id, &tp->name);

        task->next = NULL;

        ngx_spinlock(&ngx_thread_pool_done_lock, 1, 2048);

        /* ���������˵�������뵽ngx_thread_pool_done������ */
        *ngx_thread_pool_done.last = task;
        ngx_thread_pool_done.last = &task->next;

        ngx_memory_barrier();

        ngx_unlock(&ngx_thread_pool_done_lock);

        /* 
         * ��ngx_thread_pool_done�����е���������β����������epoll��˵��
         * ngx_notify����ָngx_epoll_notify()���� 
         */
        (void) ngx_notify(ngx_thread_pool_handler);
    }
}

/* ��ngx_thread_pool_done�����е���������β���� */
static void
ngx_thread_pool_handler(ngx_event_t *ev)
{
    ngx_event_t        *event;
    ngx_thread_task_t  *task;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ev->log, 0, "thread pool handler");

    ngx_spinlock(&ngx_thread_pool_done_lock, 1, 2048);

    task = ngx_thread_pool_done.first;
    ngx_thread_pool_done.first = NULL;
    ngx_thread_pool_done.last = &ngx_thread_pool_done.first;

    ngx_memory_barrier();

    ngx_unlock(&ngx_thread_pool_done_lock);

    /* ѭ������ngx_thread_pool_done�����е���������β���� */
    while (task) {
        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0,
                       "run completion handler for task #%ui", task->id);

        /* ��ȡ�����Ӧ���¼����� */
        event = &task->event;
        task = task->next;

        /* ����ʾ�¼��첽������ɵı�־��λ */
        event->complete = 1;
        event->active = 0;

        /* �����¼������� */
        event->handler(event);
    }
}

/* �������ڴ��ngx_thread_pool_moduleģ��������Ϣ���ڴ� */
static void *
ngx_thread_pool_create_conf(ngx_cycle_t *cycle)
{
    ngx_thread_pool_conf_t  *tcf;

    tcf = ngx_pcalloc(cycle->pool, sizeof(ngx_thread_pool_conf_t));
    if (tcf == NULL) {
        return NULL;
    }

    if (ngx_array_init(&tcf->pools, cycle->pool, 4,
                       sizeof(ngx_thread_pool_t *))
        != NGX_OK)
    {
        return NULL;
    }

    return tcf;
}

/* ������������֮����� */
static char *
ngx_thread_pool_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_thread_pool_conf_t *tcf = conf;

    ngx_uint_t           i;
    ngx_thread_pool_t  **tpp;

    tpp = tcf->pools.elts;

    for (i = 0; i < tcf->pools.nelts; i++) {

        if (tpp[i]->threads) {
            continue;
        }

        /* ���Ĭ�ϵ�thread_poolû�������߳����Ͷ��г��ȣ�������ΪĬ��ֵ */
        if (tpp[i]->name.len == ngx_thread_pool_default.len
            && ngx_strncmp(tpp[i]->name.data, ngx_thread_pool_default.data,
                           ngx_thread_pool_default.len)
               == 0)
        {
            tpp[i]->threads = 32;
            tpp[i]->max_queue = 65536;
            continue;
        }

        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "unknown thread pool \"%V\" in %s:%ui",
                      &tpp[i]->name, tpp[i]->file, tpp[i]->line);

        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

/* thread_pool����Ľ������� */
static char *
ngx_thread_pool(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t          *value;
    ngx_uint_t          i;
    ngx_thread_pool_t  *tp;

    /* ����������ļ���thread_pool�����������Ϣ����һ��Ԫ��Ϊ������������Ϊ������� */
    value = cf->args->elts;

    /* ���һ���̳߳ض����ڴ��� */
    tp = ngx_thread_pool_add(cf, &value[1]);

    if (tp == NULL) {
        return NGX_CONF_ERROR;
    }

    /* ���tp->threads��Ϊ0��˵��֮ǰ�Ѿ�������ͬ�����̳߳���Ϣ�����Է���ʧ�� */
    if (tp->threads) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "duplicate thread pool \"%V\"", &tp->name);
        return NGX_CONF_ERROR;
    }

    /* Ĭ�������tp->max_queueΪ65536 */
    tp->max_queue = 65536;

    for (i = 2; i < cf->args->nelts; i++) {

        /* �����̳߳�������Ϣ */
        if (ngx_strncmp(value[i].data, "threads=", 8) == 0) {

            tp->threads = ngx_atoi(value[i].data + 8, value[i].len - 8);

            if (tp->threads == (ngx_uint_t) NGX_ERROR || tp->threads == 0) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid threads value \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }

            continue;
        }

        /* �����ȴ����г�����Ϣ */
        if (ngx_strncmp(value[i].data, "max_queue=", 10) == 0) {

            tp->max_queue = ngx_atoi(value[i].data + 10, value[i].len - 10);

            if (tp->max_queue == NGX_ERROR) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid max_queue value \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }

            continue;
        }
    }

    /* ��������ļ���û�������̳߳����̵߳��������򷵻�ʧ�� */
    if (tp->threads == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"%V\" must have \"threads\" parameter",
                           &cmd->name);
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}

/* ��ngx_thread_pool_moduleģ���������Ϣ�����һ���̳߳ض��� */
ngx_thread_pool_t *
ngx_thread_pool_add(ngx_conf_t *cf, ngx_str_t *name)
{
    ngx_thread_pool_t       *tp, **tpp;
    ngx_thread_pool_conf_t  *tcf;

    /* ���û���������֣������Ĭ�ϵ����� */
    if (name == NULL) {
        name = &ngx_thread_pool_default;
    }

    /* 
     * �����������ļ������õ��̳߳�����ȥ�ڴ��в����Ƿ�����ͬ���ֵ��̳߳أ�
     * ����У��򷵻أ����򽫵�ǰ�̳߳���ӵ��ڴ���
     */
    tp = ngx_thread_pool_get(cf->cycle, name);

    if (tp) {
        return tp;
    }

    tp = ngx_pcalloc(cf->pool, sizeof(ngx_thread_pool_t));
    if (tp == NULL) {
        return NULL;
    }

    tp->name = *name;
    
    /* ���������ļ������ֺ�thread_pool�����������ļ��е��к� */
    tp->file = cf->conf_file->file.name.data;
    tp->line = cf->conf_file->line;

    /* ��ȡngx_thread_pool_moduleģ���������ṹ����� */
    tcf = (ngx_thread_pool_conf_t *) ngx_get_conf(cf->cycle->conf_ctx,
                                                  ngx_thread_pool_module);

    tpp = ngx_array_push(&tcf->pools);
    if (tpp == NULL) {
        return NULL;
    }

    *tpp = tp;

    return tp;
}

/* 
 * �������֣����ԴӴ���������̳߳ض�����ڴ����ҵ���Ӧ���ֵ��̳߳ض���
 * ���û���ҵ����򷵻�NULL
 */
ngx_thread_pool_t *
ngx_thread_pool_get(ngx_cycle_t *cycle, ngx_str_t *name)
{
    ngx_uint_t                i;
    ngx_thread_pool_t       **tpp;
    ngx_thread_pool_conf_t   *tcf;

    tcf = (ngx_thread_pool_conf_t *) ngx_get_conf(cycle->conf_ctx,
                                                  ngx_thread_pool_module);

    /* �������е��̳߳ض����ҵ�����ƥ����̳߳� */
    tpp = tcf->pools.elts;
    for (i = 0; i < tcf->pools.nelts; i++) {

        if (tpp[i]->name.len == name->len
            && ngx_strncmp(tpp[i]->name.data, name->data, name->len) == 0)
        {
            return tpp[i];
        }
    }

    return NULL;
}

/* �ڳ�ʼ��worker�ӽ��̵�ʱ����� */
static ngx_int_t
ngx_thread_pool_init_worker(ngx_cycle_t *cycle)
{
    ngx_uint_t                i;
    ngx_thread_pool_t       **tpp;
    ngx_thread_pool_conf_t   *tcf;

    if (ngx_process != NGX_PROCESS_WORKER
        && ngx_process != NGX_PROCESS_SINGLE)
    {
        return NGX_OK;
    }

    /* ��ȡngx_thread_pool_moduleģ���������Ϣ */
    tcf = (ngx_thread_pool_conf_t *) ngx_get_conf(cycle->conf_ctx,
                                                  ngx_thread_pool_module);

    if (tcf == NULL) {
        return NGX_OK;
    }

    /* ��ʼ���������ngx_thread_pool_done */
    ngx_thread_pool_queue_init(&ngx_thread_pool_done);

    /* ���ս������Ⱥ�˳�����γ�ʼ�������ļ������õ�ÿһ���̳߳� */
    tpp = tcf->pools.elts;
    for (i = 0; i < tcf->pools.nelts; i++) {
        if (ngx_thread_pool_init(tpp[i], cycle->log, cycle->pool) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

/* ��worker�ӽ����˳���ʱ����� */
static void
ngx_thread_pool_exit_worker(ngx_cycle_t *cycle)
{
    ngx_uint_t                i;
    ngx_thread_pool_t       **tpp;
    ngx_thread_pool_conf_t   *tcf;

    if (ngx_process != NGX_PROCESS_WORKER
        && ngx_process != NGX_PROCESS_SINGLE)
    {
        return;
    }

    tcf = (ngx_thread_pool_conf_t *) ngx_get_conf(cycle->conf_ctx,
                                                  ngx_thread_pool_module);

    if (tcf == NULL) {
        return;
    }

    /* �������е��̳߳� */
    tpp = tcf->pools.elts;
    for (i = 0; i < tcf->pools.nelts; i++) {
        ngx_thread_pool_destroy(tpp[i]);
    }
}

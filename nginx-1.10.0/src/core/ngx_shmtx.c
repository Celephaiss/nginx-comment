
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>

/*
 * ngx_shmtx_t�е�lock��ʾ��ǰ����״̬�����Ϊ0����ʾ��ǰû�н��̳�����
 * ��lockֵΪ������ʱ�򣬱�ʾ�н�����������
 */

#if (NGX_HAVE_ATOMIC_OPS) //��ԭ�ӱ���ʵ��ngx_shmtx_t������


static void ngx_shmtx_wakeup(ngx_shmtx_t *mtx);


ngx_int_t
ngx_shmtx_create(ngx_shmtx_t *mtx, ngx_shmtx_sh_t *addr, u_char *name)
{
    /*��ʵ��һ�����ǰѹ����ڴ���׵�ַ��ֵ����ԭ�ӱ�����*/
    mtx->lock = &addr->lock;

    /*mtx->spinΪ-1ʱ����ʾ����ʹ���ź�����ֱ�ӷ��سɹ�*/
    if (mtx->spin == (ngx_uint_t) -1) {
        return NGX_OK;
    }

    /*spin��ʾ���������ȴ������������ͷ�����ʱ��*/
    mtx->spin = 2048;

#if (NGX_HAVE_POSIX_SEM)

    mtx->wait = &addr->wait;

    /*
     * int  sem init (sem_t  sem,  int pshared,  unsigned int value) ,
     * ���У�����sem��Ϊ���Ƕ�����ź�����������pshared��ָ��sem�ź���������
     * ���̼�ͬ�����������̼߳�ͬ������psharedΪ0ʱ��ʾ�̼߳�ͬ����
     * ��psharedΪ1ʱ��ʾ���̼�ͬ��������Nginx��ÿ�����̶��ǵ��̵߳ģ�
     * ��˽�����pshared��Ϊ1���ɡ�����value��ʾ�ź���sem�ĳ�ʼֵ��
     */
    /*�Զ����ʹ�õķ�ʽ��ʼ��sem�ź�����sem��ʼֵΪ0*/
    if (sem_init(&mtx->sem, 1, 0) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno,
                      "sem_init() failed");
    } else {
        mtx->semaphore = 1;  //�ź����ɹ���ʼ����semaphore��Ϊ1����ʾ��ȡ��ʱ��ʹ���ź���
    }

#endif

    return NGX_OK;
}


/*�÷�����ΨһĿ�ľ����ͷ��ź���*/
void
ngx_shmtx_destroy(ngx_shmtx_t *mtx)
{
#if (NGX_HAVE_POSIX_SEM)

    //mtx->spin ��Ϊ-1�����ź�����ʼ���ɹ�ʱ��mtx->semaphore��Ϊ1
    if (mtx->semaphore) {
        if (sem_destroy(&mtx->sem) == -1) {
            ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno,
                          "sem_destroy() failed");
        }
    }

#endif
}

/*
 * *mtx->lock == 0 && ngx_atomic_cmp_set(mtx->lock, 0, ngx_pid)
 * *mtx->lock == 0����Ŀǰû�н��̳�����������̵�Nginx�����п��ܳ����������:
 * ���ǵ�һ�����(*mtx->lock == 0)ִ�гɹ�,����ִ�еڶ������ǰ������һ�������õ�������
 * ��ʱ�ڶ�������ִ��ʧ�ܣ�������ngx_atomic_cmp_set�����������ж�lockֵ�Ƿ�Ϊ0��ԭ��
 * ֻ��lockֵ��Ϊ0�����ܳɹ���ȡ�������ɹ�����lockֵΪ��ǰ���̵�id
 */
 
/*
 * �˷���Ϊ������������������û�л�ȡ�������᷵�أ�����1��ʾ��ȡ����������0��ʾû��
 */
ngx_uint_t
ngx_shmtx_trylock(ngx_shmtx_t *mtx)
{
    return (*mtx->lock == 0 && ngx_atomic_cmp_set(mtx->lock, 0, ngx_pid));
}


/*
 * �˷���Ϊ����������û�л�ȡ�������᷵�أ����ر�ʾ�Ѿ���ȡ������
 */
void
ngx_shmtx_lock(ngx_shmtx_t *mtx)
{
    ngx_uint_t         i, n;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0, "shmtx lock");

    /*û�л�ȡ����ʱ����һֱִ�������ѭ���ڵĴ���*/
    for ( ;; ) {

        /*���Ի�ȡ������ȡ���˾ͷ��أ����ж���������˼�������Ѿ�����*/
        if (*mtx->lock == 0 && ngx_atomic_cmp_set(mtx->lock, 0, ngx_pid)) {
            return;
        }

        /*��ʾ�жദ����������spinֵҲֻ���ڶദ��������²������壬����pauseָ���ִ��*/
        if (ngx_ncpu > 1) {

            /*����û�л�ȡ�����ȴ���ʱ��Խ��������ִ�и����pauseִ�к�Ż��ٴγ��Ի�ȡ��(��ȡ����ʱ������Խ��)*/
            for (n = 1; n < mtx->spin; n <<= 1) {

                for (i = 0; i < n; i++) {
                    ngx_cpu_pause();  //���ڶദ����ϵͳ��ִ��ngx_cpu_pause()�ɽ��͹���
                }

                /*���Ի�ȡ��*/
                if (*mtx->lock == 0
                    && ngx_atomic_cmp_set(mtx->lock, 0, ngx_pid))
                {
                    return;
                }
            }
        }

#if (NGX_HAVE_POSIX_SEM)

        /*semaphore�ֶ�Ϊ1����ʾ��ʹ���ź���*/
        if (mtx->semaphore) {
            (void) ngx_atomic_fetch_add(mtx->wait, 1);

            /*���Ի�ȡ��*/
            if (*mtx->lock == 0 && ngx_atomic_cmp_set(mtx->lock, 0, ngx_pid)) {
                (void) ngx_atomic_fetch_add(mtx->wait, -1);
                return;
            }

            ngx_log_debug1(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                           "shmtx wait %uA", *mtx->wait);

            /*
             * sem_wait����Ҳ��һ��ԭ�Ӳ��������������Ǵ��ź�����ֵ��ȥһ����1����
             * ������Զ���ȵȴ����ź���Ϊһ������ֵ�ſ�ʼ��������Ҳ����˵�����sem
             * ��ֵ����0����ú����Ὣ��semֵ��һ����ʾ�õ����ź�����������Ȼ���������أ�
             * �����sem��ֵΪ0���߸�����������һֱ����(˯��)���ȴ��������̰�semֵ��1��
             * �ȴ�����ϵͳ���ȵ��������ʱ������һ����������
             * �������óɹ�����0������ʧ�ܵĻ�����-1���ź���sem��ֵ���䣬��������errnoָʾ
             * sem_wait()�������ܻ�ʹ���̽���˯��״̬����ʹ��ǰ����"�ó�"������
             */
            while (sem_wait(&mtx->sem) == -1) {
                ngx_err_t  err;

                err = ngx_errno;

                /*NGX_EINTR,�������źŴ����жϣ������ǳ���*/
                if (err != NGX_EINTR) {
                    ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, err,
                                  "sem_wait() failed while waiting on shmtx");
                    break;
                }
            }

            ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                           "shmtx awoke");

            //�˴���continue����Ϊ��ʹ�����ź���ʱ�Ͳ����ٵ���ngx_sched_yiled()
            continue;
        }

#endif

        //�ڲ�ʹ���ź���ʱ���������������ʹ��ǰ������ʱ"�ó�"������
        ngx_sched_yield();
    }
}


void
ngx_shmtx_unlock(ngx_shmtx_t *mtx)
{
    /*spin != -1��ʾ�ڶദ����״̬�»������ȴ���ȡ��*/
    if (mtx->spin != (ngx_uint_t) -1) {
        ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0, "shmtx unlock");
    }

    /*�ú��������жϵ�ǰ���Ƿ񱻵�ǰ����ӵ�У�����ǣ�������0����ʾ�ͷŸ���*/
    /*ngx_atomic_cmp_set���óɹ�����1*/
    if (ngx_atomic_cmp_set(mtx->lock, ngx_pid, 0)) {
        ngx_shmtx_wakeup(mtx);
    }
}

/*ǿ���Ի����*/
/*
 * ��ngx_shmtx_lock��������һ��ʱ��������������ʼ�ղ��ͷ�������ô��ǰ��ǹ�
 */
ngx_uint_t
ngx_shmtx_force_unlock(ngx_shmtx_t *mtx, ngx_pid_t pid)
{
    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                   "shmtx forced unlock");

    if (ngx_atomic_cmp_set(mtx->lock, pid, 0)) {
        ngx_shmtx_wakeup(mtx);
        return 1;
    }

    return 0;
}

/*���ѽ���*/
static void
ngx_shmtx_wakeup(ngx_shmtx_t *mtx)
{
#if (NGX_HAVE_POSIX_SEM)
    ngx_atomic_uint_t  wait;

    /*semaphore��־λΪ0����ʾ��ʹ���ź�������������*/
    if (!mtx->semaphore) {
        return;
    }

    for ( ;; ) {

        wait = *mtx->wait;

        /*���lock��ԭ�ȵ�ֵΪ0��Ҳ����˵û�н��̳��и�����ֱ�ӷ���*/
        if ((ngx_atomic_int_t) wait <= 0) {
            return;
        }

        /*��waitֵ����Ϊԭ����ֵ��һ*/
        if (ngx_atomic_cmp_set(mtx->wait, wait, wait - 1)) {
            break;
        }
    }

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, ngx_cycle->log, 0,
                   "shmtx wake %uA", wait);

    /*ͨ��sem_post���ź�����ֵ��1����ʾ��ǰ�����Ѿ��ͷ����ź�����������֪ͨ�������̵�sem_wait����ִ��*/
    if (sem_post(&mtx->sem) == -1) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno,
                      "sem_post() failed while wake shmtx");
    }

#endif
}


#else
/*���ļ���ʵ�ֵĻ�����*/


ngx_int_t
ngx_shmtx_create(ngx_shmtx_t *mtx, ngx_shmtx_sh_t *addr, u_char *name)
{
    if (mtx->name) {

        if (ngx_strcmp(name, mtx->name) == 0) {
            mtx->name = name;
            return NGX_OK;
        }

        ngx_shmtx_destroy(mtx);
    }

    mtx->fd = ngx_open_file(name, NGX_FILE_RDWR, NGX_FILE_CREATE_OR_OPEN,
                            NGX_FILE_DEFAULT_ACCESS);

    if (mtx->fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_EMERG, ngx_cycle->log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", name);
        return NGX_ERROR;
    }

    if (ngx_delete_file(name) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno,
                      ngx_delete_file_n " \"%s\" failed", name);
    }

    mtx->name = name;

    return NGX_OK;
}


void
ngx_shmtx_destroy(ngx_shmtx_t *mtx)
{
    if (ngx_close_file(mtx->fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", mtx->name);
    }
}


ngx_uint_t
ngx_shmtx_trylock(ngx_shmtx_t *mtx)
{
    ngx_err_t  err;

    err = ngx_trylock_fd(mtx->fd);

    if (err == 0) {
        return 1;
    }

    if (err == NGX_EAGAIN) {
        return 0;
    }

#if __osf__ /* Tru64 UNIX */

    if (err == NGX_EACCES) {
        return 0;
    }

#endif

    ngx_log_abort(err, ngx_trylock_fd_n " %s failed", mtx->name);

    return 0;
}


void
ngx_shmtx_lock(ngx_shmtx_t *mtx)
{
    ngx_err_t  err;

    err = ngx_lock_fd(mtx->fd);

    if (err == 0) {
        return;
    }

    ngx_log_abort(err, ngx_lock_fd_n " %s failed", mtx->name);
}


void
ngx_shmtx_unlock(ngx_shmtx_t *mtx)
{
    ngx_err_t  err;

    err = ngx_unlock_fd(mtx->fd);

    if (err == 0) {
        return;
    }

    ngx_log_abort(err, ngx_unlock_fd_n " %s failed", mtx->name);
}


ngx_uint_t
ngx_shmtx_force_unlock(ngx_shmtx_t *mtx, ngx_pid_t pid)
{
    return 0;
}

#endif

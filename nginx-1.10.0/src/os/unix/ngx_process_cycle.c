
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_channel.h>


static void ngx_start_worker_processes(ngx_cycle_t *cycle, ngx_int_t n,
    ngx_int_t type);
static void ngx_start_cache_manager_processes(ngx_cycle_t *cycle,
    ngx_uint_t respawn);
static void ngx_pass_open_channel(ngx_cycle_t *cycle, ngx_channel_t *ch);
static void ngx_signal_worker_processes(ngx_cycle_t *cycle, int signo);
static ngx_uint_t ngx_reap_children(ngx_cycle_t *cycle);
static void ngx_master_process_exit(ngx_cycle_t *cycle);
static void ngx_worker_process_cycle(ngx_cycle_t *cycle, void *data);
static void ngx_worker_process_init(ngx_cycle_t *cycle, ngx_int_t worker);
static void ngx_worker_process_exit(ngx_cycle_t *cycle);
static void ngx_channel_handler(ngx_event_t *ev);
static void ngx_cache_manager_process_cycle(ngx_cycle_t *cycle, void *data);
static void ngx_cache_manager_process_handler(ngx_event_t *ev);
static void ngx_cache_loader_process_handler(ngx_event_t *ev);


ngx_uint_t    ngx_process;  //��ǰ�ӽ��̵�����
ngx_uint_t    ngx_worker;  //��ǰ��worker�ӽ��̵ı��
ngx_pid_t     ngx_pid;  //��ǰ�ӽ��̵�id

sig_atomic_t  ngx_reap;  //�յ�CHLD�źţ���ʱ���ӽ��������������Ҫ������е��ӽ���
sig_atomic_t  ngx_sigio;
sig_atomic_t  ngx_sigalrm;  //�ȴ��ӽ����˳��Ķ�ʱ����ʱ�źű�־λ
sig_atomic_t  ngx_terminate;  //�յ�TERM��INT�ź�ʱ��ǿ�ƹرս���
sig_atomic_t  ngx_quit;  //�յ�QUIT�ź�ʱ�����ŵعرս���
sig_atomic_t  ngx_debug_quit;  //WINCH�ź�
ngx_uint_t    ngx_exiting;  //˵����ʼ׼���ر�worker����
sig_atomic_t  ngx_reconfigure;  //�յ�SIGHUP�ź�ʱ�����¼��������ļ�
sig_atomic_t  ngx_reopen;  //�յ�USR1�ź�ʱ�����´������ļ�

sig_atomic_t  ngx_change_binary;  //�յ�USR2�źţ�ƽ���������°汾��nginx����
ngx_pid_t     ngx_new_binary;  //ƽ��������ʱ������ִ��ϵͳ����execve���ӽ��̵�id,Ҳ���°汾nginx�����id
ngx_uint_t    ngx_inherited;
ngx_uint_t    ngx_daemonized;

sig_atomic_t  ngx_noaccept;  //�յ�WINCH�źţ����е��ӽ��̲��ٽ��ܴ����µ����ӣ��൱�����ӽ��̷���QUIT�ź�
ngx_uint_t    ngx_noaccepting;  //�����ӽ������ڲ������µ�����
ngx_uint_t    ngx_restart;  //��master������������Ϊ��־λʹ�ã����ź��޹�


static u_char  master_process[] = "master process";


static ngx_cache_manager_ctx_t  ngx_cache_manager_ctx = {
    ngx_cache_manager_process_handler, "cache manager process", 0
};

static ngx_cache_manager_ctx_t  ngx_cache_loader_ctx = {
    ngx_cache_loader_process_handler, "cache loader process", 60000
};


static ngx_cycle_t      ngx_exit_cycle;
static ngx_log_t        ngx_exit_log;
static ngx_open_file_t  ngx_exit_log_file;

/*
 * master���̲���Ҫ���������¼��Ͷ�ʱ���¼�����������ҵ���ִ�У�ֻ��ͨ������worker������ʵ����������
 * ƽ��������������־�ļ��������ļ�ʵʱ��Ч�ȹ���
 */
void
ngx_master_process_cycle(ngx_cycle_t *cycle)
{
    char              *title;
    u_char            *p;
    size_t             size;
    ngx_int_t          i;
    ngx_uint_t         n, sigio;
    sigset_t           set;
    struct itimerval   itv;
    ngx_uint_t         live;
    ngx_msec_t         delay;
    ngx_listening_t   *ls;
    ngx_core_conf_t   *ccf;

    /*ע�ᴦ���ź�*/
    /*
     * sigemptyset ������ʼ���źż���set,��set ����Ϊ��.
     * sigfillset Ҳ��ʼ���źż���,ֻ�ǽ��źż�������Ϊ�����źŵļ���.
     * sigaddset ���ź�signo ���뵽�źż���֮��,sigdelset ���źŴ��źż�����ɾ��.
     * sigismember ��ѯ�ź��Ƿ����źż���֮��.
     * sigprocmask ����Ϊ�ؼ���һ������.��ʹ��֮ǰҪ�����ú��źż���set.��������������ǽ�ָ�����źż���set 
     * ���뵽���̵��ź���������֮��ȥ,����ṩ��oset ��ô��ǰ�Ľ����ź��������Ͻ��ᱣ����oset ����.����how 
     * ���������Ĳ�����ʽ��
     *      SIG_BLOCK������һ���źż��ϵ���ǰ���̵���������֮��.
     *      SIG_UNBLOCK���ӵ�ǰ����������֮��ɾ��һ���źż���.
     *      SIG_SETMASK������ǰ���źż�������Ϊ�ź���������.
    */
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGALRM);
    sigaddset(&set, SIGIO);
    sigaddset(&set, SIGINT);
    sigaddset(&set, ngx_signal_value(NGX_RECONFIGURE_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_REOPEN_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_NOACCEPT_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_TERMINATE_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_CHANGEBIN_SIGNAL));

    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "sigprocmask() failed");
    }

    sigemptyset(&set);


    /*�������޸�master�������ֵĲ���*/
    
    /*1.����argv������ܳ���*/
    size = sizeof(master_process);

    for (i = 0; i < ngx_argc; i++) {
        size += ngx_strlen(ngx_argv[i]) + 1;
    }

    title = ngx_pnalloc(cycle->pool, size);
    if (title == NULL) {
        /* fatal */
        exit(2);
    }
    /*ִ����ngx_cpymem��pָ����title��һ�θ��ƿ�ʼ�ĵ�ַ*/
    p = ngx_cpymem(title, master_process, sizeof(master_process) - 1);
    for (i = 0; i < ngx_argc; i++) {
        *p++ = ' ';
        p = ngx_cpystrn(p, (u_char *) ngx_argv[i], size);
    }

    /*�޸Ľ�������*/
    ngx_setproctitle(title);


    /*��ȡ����ģ��洢�������ָ��*/
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    /*����������worker�ӽ���*/
    ngx_start_worker_processes(cycle, ccf->worker_processes,
                               NGX_PROCESS_RESPAWN);
    /*����������cache_manage�ӽ���*/
    ngx_start_cache_manager_processes(cycle, 0);

    ngx_new_binary = 0;
    delay = 0;
    sigio = 0;
    live = 1;

    for ( ;; ) {
        /*
        * delay�����ȴ��ӽ����˳���ʱ�䣬�������ǽ��ܵ�SIGINT�źź�������Ҫ�ȷ����źŸ��ӽ��̣����ӽ��̵��˳�
        * ��Ҫһ����ʱ�䣬��ʱʱ����ӽ������˳������Ǹ����̾�ֱ���˳���������sigkill�źŸ��ӽ���(ǿ���˳�),
        * Ȼ�����˳���
        */
        if (delay) {
            if (ngx_sigalrm) {
                sigio = 0;
                delay *= 2;
                ngx_sigalrm = 0;
            }

            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "termination cycle: %M", delay);

            itv.it_interval.tv_sec = 0;
            itv.it_interval.tv_usec = 0;
            itv.it_value.tv_sec = delay / 1000;
            itv.it_value.tv_usec = (delay % 1000 ) * 1000;

            /* 
             * int setitimer(int which, const struct itimerval *value, struct itimerval *ovalue);
             *   whichΪ��ʱ�����ͣ�setitimer֧��3�����͵Ķ�ʱ����
             *     ITIMER_REAL: ��ϵͳ��ʵ��ʱ�������㣬���ͳ�SIGALRM�źš�
             *     ITIMER_VIRTUAL: -�Ըý������û�̬�»��ѵ�ʱ�������㣬���ͳ�SIGVTALRM�źš�
             *     ITIMER_PROF: �Ըý������û�̬�º��ں�̬�����ѵ�ʱ�������㣬���ͳ�SIGPROF�źš�
             * setitimer()��һ������whichָ����ʱ�����ͣ���������֮һ�����ڶ��������ǽṹitimerval��һ��ʵ���������������ɲ�������
             * setitimer()���óɹ�����0�����򷵻�-1��
             */
            /*��ʱ*/
            if (setitimer(ITIMER_REAL, &itv, NULL) == -1) {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                              "setitimer() failed");
            }
        }

        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "sigsuspend");

        /*sigsuspend�����ڽ��յ�ĳ���ź�֮ǰ����ʱ��mask�滻���̵��ź����룬����ͣ����ִ�У�ֱ���յ��ź�Ϊֹ��*/
        /*
         * ����ִ�е�sigsuspendʱ��sigsuspend���������̷��أ����̴���TASK_INTERRUPTIBLE״̬�����̷���CPU��
         * �ȴ�UNBLOCK��mask֮��ģ��źŵĻ��ѡ������ڽ��յ�UNBLOCK��mask֮�⣩�źź󣬵��ô�������Ȼ�������
         * ���źż���ԭΪԭ���ģ�sigsuspend���أ����ָ̻�ִ�С�
         */
        sigsuspend(&set);

        /*����nginx�ں˻���ʱ��*/
        ngx_time_update();

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "wake up, sigio %i", sigio);

        /*���ӽ��������˳�����������ӽ���*/
        if (ngx_reap) {
            ngx_reap = 0;
            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "reap children");

            live = ngx_reap_children(cycle);
        }

        /*���û��worker�ӽ��̻��������Ѿ��յ���ngx_terminate����ngx_quit�źţ����˳�master����*/
        if (!live && (ngx_terminate || ngx_quit)) {
            ngx_master_process_exit(cycle);
        }

        /*�յ���ngx_terminate�ź�*/
        if (ngx_terminate) {
            if (delay == 0) {
                delay = 50;
            }

            if (sigio) {
                sigio--;
                continue;
            }

            sigio = ccf->worker_processes + 2 /* cache processes */;

            /*���ӽ��̷���TERM�ź�*/
            if (delay > 1000) {
                ngx_signal_worker_processes(cycle, SIGKILL);  //�����ʱ����ǿ��ɱ��worker  
            } else {
                ngx_signal_worker_processes(cycle,
                                       ngx_signal_value(NGX_TERMINATE_SIGNAL));
            }

            continue;
        }

        /*����յ���quit�ź�*/
        if (ngx_quit) {
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));  //���ӽ��̷���quit�ź�

            /*�رռ�����socket*/
            ls = cycle->listening.elts;
            for (n = 0; n < cycle->listening.nelts; n++) {
                if (ngx_close_socket(ls[n].fd) == -1) {
                    ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_socket_errno,
                                  ngx_close_socket_n " %V failed",
                                  &ls[n].addr_text);
                }
            }
            cycle->listening.nelts = 0;

            continue;
        }

        /*����յ���SIGHP�źţ�ngx_reconfigureΪ1����ʾ��Ҫ���¶�ȡ�����ļ�*/
        if (ngx_reconfigure) {
            ngx_reconfigure = 0;

            /*
             * ��ƽ��������ʱ����һ��ʱ����Ǿɰ汾��master���̻���°汾��master�����Լ��°汾��worker���̹��档
             * ��ʱ���ǿ��Ծ����Ǽ���ʹ�þɰ汾��nginx����ʹ���°汾��nginx��������Ǿ���ʹ���°汾��nginx����ô�ɰ汾
             * ��nginxҲ�Ͳ����յ�SIGHUP���źţ�������뵽ngx_reconfigureΪ1�ķ�֧�����ǻ��˳�����ʲô����»��ߵ�������?
             * ��ʵ�ߵ���������Ϊ����ѡ���˲�ʹ���°汾��nginx��������ʹ�þɰ汾��nginx����������ͨ��kill������ϰ汾��nginx
             * ������SIGHUP�źţ�ngx_reconfigure����λ������������֮ǰ����ƽ������������ngx_new_binary�����°汾nginx���̵�id
             * ���ھɰ汾��worker���̴����������Ѿ��˳��ˣ����������Ҫ��worker��cache�ӽ��̶�������������ʼ�����µ�����
             * ��ʵ������̾���ƽ��������nginx ���ڲ����������ļ���������������Ĺ������� 
             */
            if (ngx_new_binary) {
                ngx_start_worker_processes(cycle, ccf->worker_processes,
                                           NGX_PROCESS_RESPAWN);
                ngx_start_cache_manager_processes(cycle, 0);
                ngx_noaccepting = 0;

                continue;
            }

            /*
             * ����ִ�е������������ƽ��������ֻ�������ļ���������
             * nginx��ʱ��ȡ�Ĳ����ǲ�����ԭ����worker���ӽ��������¶�ȡ�����ļ����������³�ʼ��ngx_cycle_t�ṹ�壬
             * ���������¶�ȡ�����ļ����������µ�worker�ӽ��̣����پ͵�worker���̡�
             */

            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reconfiguring");

            /*��ȡ�µ������ļ���ʼ��ngx_cycle_t�ṹ��*/
            /*cycleԭ��ָ����ڴ沢û���ͷţ������nginx���ڴ��ͳһ�ͷ�*/
            cycle = ngx_init_cycle(cycle);
            if (cycle == NULL) {
                cycle = (ngx_cycle_t *) ngx_cycle;
                continue;
            }

            ngx_cycle = cycle;  //��ֵ��master���̵�ngx_cycle
            ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx,
                                                   ngx_core_module);

            /*�����µ������ļ�����worker��cache_manage�ӽ���*/
            ngx_start_worker_processes(cycle, ccf->worker_processes,
                                       NGX_PROCESS_JUST_RESPAWN);  //NGX_PROCESS_JUST_RESPAWN�����������ļ��������
            ngx_start_cache_manager_processes(cycle, 1);

            /* allow new processes to start */
            ngx_msleep(100);

            live = 1;
            /*�˴�ֻ��رն�ȡ�����ļ�ǰ�ʹ��ڵ���worker�ӽ��̣��մ�����worker�ӽ��̲��رգ���ʵ�Ƿ���quit�ź�*/
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
        }

        /*
         * ngx_restartֻ����ngx_reap_children()�����б���λ���������λ��˵����ƽ������ʧ���ˣ���Ҫ���������ӽ���
         */
        if (ngx_restart) {
            ngx_restart = 0;
            ngx_start_worker_processes(cycle, ccf->worker_processes,
                                       NGX_PROCESS_RESPAWN);  //����worker�ӽ���
            ngx_start_cache_manager_processes(cycle, 0);  //����cache_manage�ӽ���
            live = 1;  //��live��־λ��1����ʾ���ӽ��̴��
        }

        /*���´��ļ��ź�USR1*/
        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, ccf->user);  //��master�����е��ļ�
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_REOPEN_SIGNAL));  //��worker�ӽ��̷���REOPEN�ź�
        }

        /*�յ�USR2�źţ���ʾҪƽ����������*/
        if (ngx_change_binary) {
            ngx_change_binary = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "changing binary");
            ngx_new_binary = ngx_exec_new_binary(cycle, ngx_argv);
        }

        /*
         * ��ʾҪ�����е�worker�ӽ������ŵعرս��̡�Ŀǰ����ƽ��������ʱ�����Ҫ�þɰ汾��nginx���������Ƴ���
         * �����ͨ��kill ������ɰ汾nginx���̷���WINCH���Ȼ��ngx_noaccept��λ
         */
        if (ngx_noaccept) {
            ngx_noaccept = 0;
            ngx_noaccepting = 1;  //��ngx_noaccepting��Ϊ1����ʾ����ֹͣ�����µ�����
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));  //��worker�ӽ��̷���QUIT�ź�
        }
    }
}


void
ngx_single_process_cycle(ngx_cycle_t *cycle)
{
    ngx_uint_t  i;

    if (ngx_set_environment(cycle, NULL) == NULL) {
        /* fatal */
        exit(2);
    }

    /*��������ģ���init_process�ص�����*/
    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->init_process) {
            if (cycle->modules[i]->init_process(cycle) == NGX_ERROR) {
                /* fatal */
                exit(2);
            }
        }
    }

    /*������ģʽ����ѭ��*/
    for ( ;; ) {
        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "worker cycle");

        /*�¼��ռ��ͷַ����*/
        ngx_process_events_and_timers(cycle);

        /*�յ�ǿ�ƹرջ������Źرս����źţ���������ģ���exit_process�ص�����*/
        if (ngx_terminate || ngx_quit) {

            for (i = 0; cycle->modules[i]; i++) {
                if (cycle->modules[i]->exit_process) {
                    cycle->modules[i]->exit_process(cycle);
                }
            }

            /*��һЩ�˳�����ǰ���������*/
            ngx_master_process_exit(cycle);
        }

        /*���¼��������ļ����ź�*/
        if (ngx_reconfigure) {
            ngx_reconfigure = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reconfiguring");

            /*��ʼ������Ψһ�ĺ��Ľṹ��ngx_cycle_t*/
            cycle = ngx_init_cycle(cycle);
            if (cycle == NULL) {
                cycle = (ngx_cycle_t *) ngx_cycle;
                continue;
            }

            /*����ʼ���õ�cycle����ngx_cycle*/
            ngx_cycle = cycle;
        }

        /*���´������ļ����ź�*/
        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, (ngx_uid_t) -1);
        }
    }
}

/*fork�ӽ��̣������ӽ����е��ô�����*/
static void
ngx_start_worker_processes(ngx_cycle_t *cycle, ngx_int_t n, ngx_int_t type)
{
    ngx_int_t      i;
    ngx_channel_t  ch;

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "start worker processes");

    ngx_memzero(&ch, sizeof(ngx_channel_t));

    ch.command = NGX_CMD_OPEN_CHANNEL;

    for (i = 0; i < n; i++) {  //�������е�worker�ӽ���

        ngx_spawn_process(cycle, ngx_worker_process_cycle,
                          (void *) (intptr_t) i, "worker process", type);

        //�����������Ѿ��������ӽ��̹㲥��ǰ�ӽ��̵���Ϣ
        ch.pid = ngx_processes[ngx_process_slot].pid;  //ngx_process_slot��ngx_spawn_process�б�����Ϊ��ǰ�������ӽ���
        ch.slot = ngx_process_slot;
        ch.fd = ngx_processes[ngx_process_slot].channel[0];   //��Ӧ��ǰ�ӽ��̵ĸ�����ʹ��channel[0]�׽���

        ngx_pass_open_channel(cycle, &ch);  //���ڸ��������ӽ��̹��ڱ��ӽ��̵�Ƶ��ͨѶ��Ϣ
    }
}

/*
 * ��nginx�У����������proxy(��fastcgi) cache���ܣ���ômaster���������������л����������ӽ��̣���
 * cache_manager��cache_loader�ӽ��̣����������ڴ�ʹ��̵Ļ�����塣cache_manager���̵������Ƕ��ڼ��
 * ���棬�������ڵĻ��������cache_loader���̵���������������ʱ���Ѿ������ڴ����еĸ���ӳ�䵽�ڴ���,
 * ĿǰNginx�趨Ϊ�����Ժ�60�룬Ȼ���˳���
 */

/*����cache_manager��cache_loader����*/
static void
ngx_start_cache_manager_processes(ngx_cycle_t *cycle, ngx_uint_t respawn)
{
    ngx_uint_t       i, manager, loader;
    ngx_path_t     **path;
    ngx_channel_t    ch;

    manager = 0;
    loader = 0;

    path = ngx_cycle->paths.elts;
    for (i = 0; i < ngx_cycle->paths.nelts; i++) {

        if (path[i]->manager) {  //�����Ƿ�����cache manager���� 
            manager = 1;  
        }

        if (path[i]->loader) {  //�����Ƿ�����cache loader���� 
            loader = 1;
        }
    }

    if (manager == 0) {
        return;
    }

    /*����cache manger�ӽ���*/
    ngx_spawn_process(cycle, ngx_cache_manager_process_cycle,
                      &ngx_cache_manager_ctx, "cache manager process",
                      respawn ? NGX_PROCESS_JUST_RESPAWN : NGX_PROCESS_RESPAWN);

    /*֪ͨ�����ӽ��̹���cache manager���̵���Ϣ*/
    ngx_memzero(&ch, sizeof(ngx_channel_t));

    ch.command = NGX_CMD_OPEN_CHANNEL;
    ch.pid = ngx_processes[ngx_process_slot].pid;
    ch.slot = ngx_process_slot;
    ch.fd = ngx_processes[ngx_process_slot].channel[0];

    ngx_pass_open_channel(cycle, &ch);

    if (loader == 0) {
        return;
    }

    ngx_spawn_process(cycle, ngx_cache_manager_process_cycle,
                      &ngx_cache_loader_ctx, "cache loader process",
                      respawn ? NGX_PROCESS_JUST_SPAWN : NGX_PROCESS_NORESPAWN);

    /*֪ͨ�����ӽ��̹���cache loader���̵���Ϣ*/
    ch.command = NGX_CMD_OPEN_CHANNEL;
    ch.pid = ngx_processes[ngx_process_slot].pid;
    ch.slot = ngx_process_slot;
    ch.fd = ngx_processes[ngx_process_slot].channel[0];

    ngx_pass_open_channel(cycle, &ch);
}

/*�㲥channel��Ϣ*/
static void
ngx_pass_open_channel(ngx_cycle_t *cycle, ngx_channel_t *ch)
{
    ngx_int_t  i;

    for (i = 0; i < ngx_last_process; i++) {

        //�����ոս������ӽ���(�Լ�)�ͻ�δ�������ӽ����Լ���Ӧ�������׽��ֹرյ��ӽ���
        if (i == ngx_process_slot   //ngx_process_slot��ʾ��ǰ������ӽ�����ngx_processes�е���Ҫ
            || ngx_processes[i].pid == -1
            || ngx_processes[i].channel[0] == -1)
        {
            continue;
        }

        ngx_log_debug6(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                      "pass channel s:%i pid:%P fd:%d to s:%i pid:%P fd:%d",
                      ch->slot, ch->pid, ch->fd,
                      i, ngx_processes[i].pid,
                      ngx_processes[i].channel[0]);

        /* TODO: NGX_AGAIN */
        //����֪ͨ�����ӽ��̹��ڸոմ������ӽ���(�Լ�)����Ϣ
        ngx_write_channel(ngx_processes[i].channel[0],
                          ch, sizeof(ngx_channel_t), cycle->log);
    }
}

/*master������worker�ӽ��̷���������Ϣ*/
static void
ngx_signal_worker_processes(ngx_cycle_t *cycle, int signo)
{
    ngx_int_t      i;
    ngx_err_t      err;
    ngx_channel_t  ch;

    ngx_memzero(&ch, sizeof(ngx_channel_t));

#if (NGX_BROKEN_SCM_RIGHTS)

    ch.command = 0;

#else

    /*ͨ��Ƶ��channel��worker�ӽ��̷�����Ϣ*/
    switch (signo) {

    case ngx_signal_value(NGX_SHUTDOWN_SIGNAL):
        ch.command = NGX_CMD_QUIT;
        break;

    case ngx_signal_value(NGX_TERMINATE_SIGNAL):
        ch.command = NGX_CMD_TERMINATE;
        break;

    case ngx_signal_value(NGX_REOPEN_SIGNAL):
        ch.command = NGX_CMD_REOPEN;
        break;

    default:
        ch.command = 0;
    }

#endif

    ch.fd = -1;


    for (i = 0; i < ngx_last_process; i++) {

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "child: %i %P e:%d t:%d d:%d r:%d j:%d",
                       i,
                       ngx_processes[i].pid,
                       ngx_processes[i].exiting,
                       ngx_processes[i].exited,
                       ngx_processes[i].detached,
                       ngx_processes[i].respawn,
                       ngx_processes[i].just_spawn);

        if (ngx_processes[i].detached || ngx_processes[i].pid == -1) {
            continue;
        }

        /*��ȡ�����ļ���ʱ�����ngx_start_worker_processes()��ʱ��Ὣjust_spawn��Ϊ1*/
        if (ngx_processes[i].just_spawn) {
            ngx_processes[i].just_spawn = 0;
            continue;
        }

        if (ngx_processes[i].exiting
            && signo == ngx_signal_value(NGX_SHUTDOWN_SIGNAL))
        {
            continue;
        }

        /*�����NGX_CMD_QUIT��NGX_CMD_TERINATE��NGX_CMD_REOPEN�źţ���ͨ��Ƶ�����͸�worker�ӽ���*/
        if (ch.command) {
            if (ngx_write_channel(ngx_processes[i].channel[0],
                                  &ch, sizeof(ngx_channel_t), cycle->log)
                == NGX_OK)
            {
                if (signo != ngx_signal_value(NGX_REOPEN_SIGNAL)) {
                    ngx_processes[i].exiting = 1;  //����NGX_REOPEN_SIGNAL����Ϊ�˳��źţ����ӽ���exciting��Ϊ1
                }

                continue;
            }
        }

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                       "kill (%P, %d)", ngx_processes[i].pid, signo);

        /*���ӽ��̷���signo�ź�*/
        if (kill(ngx_processes[i].pid, signo) == -1) {
            err = ngx_errno;
            ngx_log_error(NGX_LOG_ALERT, cycle->log, err,
                          "kill(%P, %d) failed", ngx_processes[i].pid, signo);

            if (err == NGX_ESRCH) {
                ngx_processes[i].exited = 1;
                ngx_processes[i].exiting = 0;
                ngx_reap = 1;
            }

            continue;
        }

        if (signo != ngx_signal_value(NGX_REOPEN_SIGNAL)) {
            ngx_processes[i].exiting = 1;
        }
    }
}

/*
 * ����ӽ��̣�����������˳�������������������������˳����򲻻���������.
 * ����ֵ���Ϊ1�������д����ӽ��̣��������ֵΪ0����������worker�ӽ��̶��˳���
 */
static ngx_uint_t
ngx_reap_children(ngx_cycle_t *cycle)
{
    ngx_int_t         i, n;
    ngx_uint_t        live;
    ngx_channel_t     ch;
    ngx_core_conf_t  *ccf;

    ngx_memzero(&ch, sizeof(ngx_channel_t));

    ch.command = NGX_CMD_CLOSE_CHANNEL;
    ch.fd = -1;

    live = 0;
    for (i = 0; i < ngx_last_process; i++) {

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "child: %i %P e:%d t:%d d:%d r:%d j:%d",
                       i,
                       ngx_processes[i].pid,
                       ngx_processes[i].exiting,
                       ngx_processes[i].exited,
                       ngx_processes[i].detached,
                       ngx_processes[i].respawn,
                       ngx_processes[i].just_spawn);

        if (ngx_processes[i].pid == -1) {  //pid == -1�����ӽ����Ѿ��˳�
            continue;
        }

        /*ngx_processes[i].exitedΪ1������ǰ�ӽ����Ѿ��˳���*/
        if (ngx_processes[i].exited) {

            /*
             * ngx_processes[i].detachedΪ0��������ӽ��̲�������ִ��execveϵͳ����ִ���°汾nginx��ִ���ļ����ӽ���,
             * ��Ϊ����ӽ��̲����������������¼��ģ����Բ���Ҫ������worker����ͨ�ţ�Ҳ�Ͳ���Ҫ��עchannel
             */
            if (!ngx_processes[i].detached) {
                ngx_close_channel(ngx_processes[i].channel, cycle->log);  //�رյ�ǰ���̺�master����ͨѶƵ��

                ngx_processes[i].channel[0] = -1;
                ngx_processes[i].channel[1] = -1;

                ch.pid = ngx_processes[i].pid;
                ch.slot = i;

                /*���������ӽ�������Ѿ��رյ��ӽ��̵���Ϣ*/
                for (n = 0; n < ngx_last_process; n++) {
                    if (ngx_processes[n].exited
                        || ngx_processes[n].pid == -1
                        || ngx_processes[n].channel[0] == -1)
                    {
                        continue;
                    }

                    ngx_log_debug3(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                                   "pass close channel s:%i pid:%P to:%P",
                                   ch.slot, ch.pid, ngx_processes[n].pid);

                    /* TODO: NGX_AGAIN */

                    ngx_write_channel(ngx_processes[n].channel[0],
                                      &ch, sizeof(ngx_channel_t), cycle->log);
                }
            }

            /*
             * ngx_processes[i].respawnΪ1������Ҫ������������ӽ���
             * ngx_processes[i].exitingΪ0��ʾ��ʱ�ӽ��̲��������˳�״̬
             * ngx_terminate����ngx_quitΪ0������master���̲�û���յ��������źţ���ȻҲ���ᷢ���������źŸ��ӽ���
             * �������������������ʱ��Ҫ���������������������ӽ���
             */
            if (ngx_processes[i].respawn
                && !ngx_processes[i].exiting
                && !ngx_terminate
                && !ngx_quit)
            {
                /*���������ӽ���*/
                if (ngx_spawn_process(cycle, ngx_processes[i].proc,
                                      ngx_processes[i].data,
                                      ngx_processes[i].name, i)
                    == NGX_INVALID_PID)
                {
                    ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                                  "could not respawn %s",
                                  ngx_processes[i].name);
                    continue;
                }


                /*�������ӽ��̹㲥����ӽ��̵���Ϣ*/
                ch.command = NGX_CMD_OPEN_CHANNEL;
                ch.pid = ngx_processes[ngx_process_slot].pid;
                ch.slot = ngx_process_slot;
                ch.fd = ngx_processes[ngx_process_slot].channel[0];

                ngx_pass_open_channel(cycle, &ch);

                live = 1;

                continue;
            }

            /*
             * ��nginx����ƽ��������ʱ�򣬻�����һ���ӽ���ר������ִ��execveϵͳ������ִ���°汾��nginx��ִ���ļ�,
             * �Ӵ���ʵ�������ǿ��Կ�����ngx_new_binary�������������ӽ��̵Ľ���id�����⣬��master���̴��������
             * �ӽ��̣�������ӽ��̽���execveϵͳ���ã�execve���ִ�гɹ��ǲ����˳��ģ����ǳ���ִ�е�����˵��execve�����ˣ�
             * Ȼ�����exit()�˳����������һ������������°汾��nginx����ʧ���ˡ���ᴥ��ϵͳ����SIGCHLD�źţ�
             * ����ngx_reap��־λΪ1���ٽ��뵽�����֧����֮������˳����ӽ���������ִ��execveϵͳ���õ��ӽ��̣�˵��
             * ƽ������ʧ���ˡ���ʱ��Ҫ�ָ��ϰ汾��nginx����������Ӧ��worker���̡�
             */
            if (ngx_processes[i].pid == ngx_new_binary) {

                ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx,
                                                       ngx_core_module);

                /* int rename(char *oldname, char *newname);*/
                /* 
                 * 2016-07-11�����׵ĵط�:Ϊʲô�������֧��Ҫ������pid�ļ����������ϰ汾��pid�ļ���ƽ������ʧ����?
                 * ����������ʧ��?
                 * 2016-07-12������:
                 * ��Ϊ�����ɰ汾master�����յ�SIGCHLD�źŵ�������ִ���°汾nginx������ӽ��̣�˵��execve�����ˣ�
                 * execveϵͳ�������ִ�гɹ��ǲ��᷵�صģ�Ҳ����˵execveִ��ʧ���ˣ������°汾nginx����ʧ�ܡ�
                 */
                if (ngx_rename_file((char *) ccf->oldpid.data,
                                    (char *) ccf->pid.data)
                    == NGX_FILE_ERROR)
                {
                    ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                                  ngx_rename_file_n " %s back to %s failed "
                                  "after the new binary process \"%s\" exited",
                                  ccf->oldpid.data, ccf->pid.data, ngx_argv[0]);
                }

                ngx_new_binary = 0;
                /*
                 * ngx_noacceptingΪ1����������ֹͣ�����µ�����.������ķ�������֪��������ִ�е�����˵��ƽ������ʧ�ܡ�
                 * ��Ҫ���ϰ汾nginx(��ʵ�����Լ�)����������У������Ҫ�����ɰ汾nginx��Ӧ��worker�ӽ��̣���ngx_restart
                 * ��־λ��Ϊ1����������ֹͣ���������ӵı�־λ���㡣
                 */
                if (ngx_noaccepting) {
                    ngx_restart = 1;  //�������̱�־λ
                    ngx_noaccepting = 0;  //�����ʾ���Խ����µ�����
                }
            }

            /*
             * ����ִ�е����˵����������������̵��ӽ����˳����������˳�������Ҫ�������𣬽�pid��Ϊ-1.
             * ��Ϊʲô������Ҫ�ٷ�����������?��Ϊ����պ�����˳����ӽ��������һ����Ч���ӽ��̣���ôֱ�ӽ�
             * ��ʾ���һ����Ч�ӽ��̵��±��1Ҳ���൱�ڰ�����ӽ�����Ϊ��Ч,���pid��Ϊ-1��Ч����һ���ġ�ֻ��
             * ��Ҫ�������⴦��
             */
            if (i == ngx_last_process - 1) {
                ngx_last_process--;

            } else {
                ngx_processes[i].pid = -1;
            }

        /*�ӽ��������˳����߲������Ǹ�����ƽ������ʹ�õ��ӽ���*/
        } else if (ngx_processes[i].exiting || !ngx_processes[i].detached) {
            live = 1;
        }
    }

    return live;
}

/*
 * *****************************�˳�master����**********************************
 * 1.ɾ��nginx.pid�ļ�
 * 2.����ģ���exit_master�ص�����
 * 3.�رռ���socket
 * 4.�ͷ��ڴ��
 */
static void
ngx_master_process_exit(ngx_cycle_t *cycle)
{
    ngx_uint_t  i;

    /*ɾ��nginx.pid�ļ�*/
    ngx_delete_pidfile(cycle);

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exit");

    /*��������ģ���exit_master�ص�����*/
    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->exit_master) {
            cycle->modules[i]->exit_master(cycle);
        }
    }

    /*�رռ����˿ڵļ���socket*/
    ngx_close_listening_sockets(cycle);

    /*
     * Copy ngx_cycle->log related data to the special static exit cycle,
     * log, and log file structures enough to allow a signal handler to log.
     * The handler may be called when standard ngx_cycle->log allocated from
     * ngx_cycle->pool is already destroyed.
     */


    ngx_exit_log = *ngx_log_get_file_log(ngx_cycle->log);

    ngx_exit_log_file.fd = ngx_exit_log.file->fd;
    ngx_exit_log.file = &ngx_exit_log_file;
    ngx_exit_log.next = NULL;
    ngx_exit_log.writer = NULL;

    ngx_exit_cycle.log = &ngx_exit_log;
    ngx_exit_cycle.files = ngx_cycle->files;
    ngx_exit_cycle.files_n = ngx_cycle->files_n;
    ngx_cycle = &ngx_exit_cycle;

    /*�ͷ��ڴ��*/
    ngx_destroy_pool(cycle->pool);

    exit(0);
}

/*worker�ӽ��̹���ѭ��*/
static void
ngx_worker_process_cycle(ngx_cycle_t *cycle, void *data)
{
    ngx_int_t worker = (intptr_t) data;  //�ӽ��̱�ţ���0��ʼ

    ngx_process = NGX_PROCESS_WORKER;
    ngx_worker = worker;

    /*��ʼ��worker�ӽ��̵�һЩ��Ϣ*/
    ngx_worker_process_init(cycle, worker);

    /*�޸Ľ�������*/
    ngx_setproctitle("worker process");

    /*�ӽ��̹���ѭ��*/
    for ( ;; ) {

        /*��ʼ׼���ر�worker���̡�*/
        if (ngx_exiting) {
            ngx_event_cancel_timers();  //����ʱ���¼��������ִ���¼�������

            /*
             * ���ngx_event_timer_rbtree������Ƿ�Ϊ�գ������Ϊ�գ�˵�������¼���Ҫ��������������ִ��,
             * ����ngx_process_events_and_timers()�����¼������Ϊ�գ�˵���Ѿ������������¼�����ʱ����
             * ngx_worker_process_exit()�����������ڴ�أ��˳�����worker����
             */
            if (ngx_event_timer_rbtree.root == ngx_event_timer_rbtree.sentinel)
            {
                ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");

                /*�˳�worker�ӽ���ǰ��һЩ����*/
                ngx_worker_process_exit(cycle);
            }
        }

        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "worker cycle");

        /*�¼��ռ��ͷַ�����*/
        ngx_process_events_and_timers(cycle);

        /*ǿ�ƹرս���*/
        if (ngx_terminate) {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");

            ngx_worker_process_exit(cycle);
        }

        /*���Źرս���*/
        if (ngx_quit) {
            ngx_quit = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                          "gracefully shutting down");
            /*�޸Ľ�������*/
            ngx_setproctitle("worker process is shutting down");

            /*ngx_exiting��־λΨһһ���������Ĵ����ڴˡ�ngx_quitֻ�����״�����Ϊ1ʱ���ŻὫngx_exiting����Ϊ1*/
            if (!ngx_exiting) {
                ngx_exiting = 1;
                ngx_close_listening_sockets(cycle);  //�رռ�����socket
                ngx_close_idle_connections(cycle);  //�رտ�������(��������Ҳ�Ǵ򿪵ģ�ֻ�Ǵ�ʱû��������Ҫ����)
            }
        }

        /*���´������ļ�*/
        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, -1);
        }
    }
}

/*��ʼ��worker�ӽ��̵�һЩ��Ϣ*/
static void
ngx_worker_process_init(ngx_cycle_t *cycle, ngx_int_t worker)
{
    sigset_t          set;
    ngx_int_t         n;
    ngx_uint_t        i;
    ngx_cpuset_t     *cpu_affinity;
    struct rlimit     rlmt;
    ngx_core_conf_t  *ccf;
    ngx_listening_t  *ls;

    /*���ý������еĻ�������*/
    if (ngx_set_environment(cycle, NULL) == NULL) {
        /* fatal */
        exit(2);
    }

    /*��ȡ����ģ��洢������Ľṹ��ָ��*/
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    /*
     * setpriority�������������ý��̡���������û��Ľ���ִ������Ȩ��
     *  
���� * ����which��������ֵ������who����whichֵ�в�ͬ���壺
��   *����which ��������who���������        whoΪ0��������� 
��   *����PRIO_PROCESS  whoΪ����ʶ����     ��whoΪ0������ý��̣�
���� *��  PRIO_PGRP ����whoΪ���̵���ʶ���� ��whoΪ0������ý��̵��飩
���� *��  PRIO_USER ����whoΪ�û�ʶ����     ��whoΪ0������ý��̵��û�ID��
���� *
���� * ����prio����-20��19֮�䡣�������ִ������Ȩ����ֵԽ�ʹ����нϸߵ����ȴ���ִ�л��Ƶ����
     * ִ�гɹ��򷵻�0������д���������ֵ��Ϊ-1������ԭ�����errno��
���� *   ESRCH ����which��who�����д����Ҳ������ϵĽ���
���� *   EINVAL ����whichֵ����
���� *   EPERM Ȩ�޲������޷��������
���� *   EACCES һ���û��޷���������Ȩ
     */

    /*�����ӽ��̵�ִ�����ȼ�*/
    if (worker >= 0 && ccf->priority != 0) {
        if (setpriority(PRIO_PROCESS, 0, ccf->priority) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setpriority(%d) failed", ccf->priority);
        }
    }

    /*
     * getrlimit()��ȡ��Դʹ������ setrlimit()������Դʹ�����ơ�ÿ����Դ������Ӧ����Ӳ����
     * ���������ں�ǿ�Ӹ���Ӧ��Դʹ�õ�����ֵ��Ӳ����ֵ��������ֵ�����ֵ������Ȩ���ý���ֻ���Խ�������ֵ
     * ����Ϊ0~Ӳ����ֵ�ķ�Χ�ڣ����ҿ��Բ�����ת�ؽ�����Ӳ����ֵ��������Ȩ���ý��̣�����������������Ӳ����ֵ
     */

    if (ccf->rlimit_nofile != NGX_CONF_UNSET) {
        rlmt.rlim_cur = (rlim_t) ccf->rlimit_nofile;  //rlmt.rlim_curΪ������
        rlmt.rlim_max = (rlim_t) ccf->rlimit_nofile;  //rlmt.rlim_maxΪӲ����
        //RLIMIT_NOFILE  ָ���Ƚ��̿ɴ򿪵�����ļ���������һ��ֵ
        if (setrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setrlimit(RLIMIT_NOFILE, %i) failed",
                          ccf->rlimit_nofile);
        }
    }

    /*���������coredump�ļ��Ĵ�С����*/
    if (ccf->rlimit_core != NGX_CONF_UNSET) {
        rlmt.rlim_cur = (rlim_t) ccf->rlimit_core;
        rlmt.rlim_max = (rlim_t) ccf->rlimit_core;
        //RLIMIT_CORE ָ���ں�ת���ļ������ֵ����coredump�ļ������ֵ
        if (setrlimit(RLIMIT_CORE, &rlmt) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setrlimit(RLIMIT_CORE, %O) failed",
                          ccf->rlimit_core);
        }
    }

    /*
     * geteuid()��ȡִ�е�ǰ������Ч���û�ʶ���룬��ʾ���ִ�г���ʱ���û�ID����Ч�û�ʶ����������������ִ�е�Ȩ��.
     * getuid()��ȡ��ǰ���̵�ʵ���û�ʶ���룬��ʾ�����ʵ��������ID.
     */
    /*setgid()���ý����û���ID setuid()���ý����û�ID*/
    if (geteuid() == 0) {  //geteuid() == 0��ʾִ�г�����û���root
        if (setgid(ccf->group) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "setgid(%d) failed", ccf->group);
            /* fatal */
            exit(2);
        }

        /*
         * ����˵�� initgroups�������������ļ���/etc/group���ж�ȡһ�������ݣ�
         * ���������ݵĳ�Ա���в���userʱ���㽫����group��ʶ������뵽�������С�
         * ����ֵ ִ�гɹ��򷵻�0��ʧ���򷵻�-1�����������errno
         */
        if (initgroups(ccf->username, ccf->group) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "initgroups(%s, %d) failed",
                          ccf->username, ccf->group);
        }

        if (setuid(ccf->user) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "setuid(%d) failed", ccf->user);
            /* fatal */
            exit(2);
        }
    }

    /*worker�ӽ��̰�˲���*/
    if (worker >= 0) {
        cpu_affinity = ngx_get_cpu_affinity(worker);

        if (cpu_affinity) {
            ngx_setaffinity(cpu_affinity, cycle->log);
        }
    }

#if (NGX_HAVE_PR_SET_DUMPABLE)

    /* allow coredump after setuid() in Linux 2.4.x */

    if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "prctl(PR_SET_DUMPABLE) failed");
    }

#endif
    /*���ĵ�ǰ����Ŀ¼*/
    if (ccf->working_directory.len) {
        if (chdir((char *) ccf->working_directory.data) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "chdir(\"%s\") failed", ccf->working_directory.data);
            /* fatal */
            exit(2);
        }
    }

    sigemptyset(&set);

    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "sigprocmask() failed");
    }

    srandom((ngx_pid << 16) ^ ngx_time());

    /*
     * disable deleting previous events for the listening sockets because
     * in the worker processes there are no events at all at this point
     */
    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        ls[i].previous = NULL;
    }

    /*��������ģ���init_process()�ص�����*/
    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->init_process) {
            if (cycle->modules[i]->init_process(cycle) == NGX_ERROR) {
                /* fatal */
                exit(2);
            }
        }
    }

    for (n = 0; n < ngx_last_process; n++) {

        /*
         * �����ngx_processes[n].pid == -1 �� ngx_processes[n].channel[1] == -1���ܳ��ֵ�ԭ������Щ�ӽ��̻�û���ü�
         * ���������Զ�Ӧ���ӽ�����Ϣ����Ч�ġ��ٸ����ӣ�����ngx_process_slotΪ0����ʾ�ǵ�һ��worker�ӽ��̣����Ǻ���
         * ���ӽ��̻�û���ü�����������������Ϣ����Ч��;�ȵ�ngx_process_slotΪ1����ʾ���ڴ������ǵڶ���worker�ӽ��̣�
         * ��ʱ��һ��worker�ӽ��̵���Ϣ����Ч�ģ���ô�ͻ�ִ�е�����ر��ӽ���1�д�master���̼̳й������ӽ���0�Ķ���socket��
         * ��Ϊ�ӽ���0�Ķ���socket���ӽ���1���ã��������ӽ���1�м̳еõ����ӽ���0��Ӧ��д��socket�������������ӽ���1��
         * �ӽ���0��������ʱʹ�á�
         */

        if (ngx_processes[n].pid == -1) {
            continue;
        }

        //����ǵ�ǰ�ӽ��̣������´���ر�������׽���
        if (n == ngx_process_slot) {
            continue;
        }

        //����ý��̵����ں�master����ͨѶ���׽��ֲ����ã������´���
        if (ngx_processes[n].channel[1] == -1) {
            continue;
        }

        /*
         * ngx_processes[]���鱣���������worker���̵���Ϣ����master���̴����ġ���Ϊworker�����Ǵ�master����
         * fork�õ��ģ����worker������Ҳ�̳���master���̵�ngx_processes[]���顣���������������飬����������worker
         * �ӽ��̵����ں�master���̽���Ƶ��ͨ�ŵĶ���socket�رգ�Ҳ����channel[1]�����ﱣ��д��channel[0]��ԭ�������ڴ�
         * worker�ӽ��̺�����worker�ӽ���ͨѶ�ã��������ӽ��̾Ϳ�������Ҫ��ʱ��ͨ��channel[0]������worker�ӽ��̷���
         * ������Ϣ������worker�ӽ��̾Ϳ��ԴӶ�Ӧ�Ķ���socket��channel[1]��ȡ���ݡ�ÿ��worker�ӽ��̶���̳�
         * ngx_processes[]���飬��������رյ�ֻ�Ǹ��ӽ����е�ngx_processes[]����������worker�ӽ��̵Ķ���socket�������˸�
         * �ӽ��̱���Ķ���socket���ں�master����ͨ�š�
         */
        if (close(ngx_processes[n].channel[1]) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "close() channel failed");
        }
    }

    /*
     * ngx_processes[]���鱣���������worker���̵���Ϣ����master���̴����ġ���Ϊworker�����Ǵ�master����
     * fork�õ��ģ����worker������Ҳ�̳���master���̵�ngx_processes[]���顣����ر��˴�master���̼̳й����Ķ�Ӧ
     * ���ӽ��̱����д��socket����Ϊд��socket��master��������������������ӽ��̱����ӽ���ֻ��Ҫ����socket�����
     * ���ӽ����н�д��socket�ر�
     */
    if (close(ngx_processes[ngx_process_slot].channel[0]) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "close() channel failed");
    }

#if 0
    ngx_last_process = 0;
#endif

    /*ngx_channel���Ƕ�Ӧ���ӽ��̵Ķ���socket����ngx_read_event��ӵ�epoll�У���master��workerͨ��Ƶ������
     * ����ʱ�����ɻ�ȡ�¼�
     */
    if (ngx_add_channel_event(cycle, ngx_channel, NGX_READ_EVENT,
                              ngx_channel_handler)
        == NGX_ERROR)
    {
        /* fatal */
        exit(2);
    }
}

/*�˳�worker�ӽ���*/
static void
ngx_worker_process_exit(ngx_cycle_t *cycle)
{
    ngx_uint_t         i;
    ngx_connection_t  *c;

    /*��������ģ���exit_process�ص�*/
    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->exit_process) {
            cycle->modules[i]->exit_process(cycle);
        }
    }

    if (ngx_exiting) {
        c = cycle->connections;
        for (i = 0; i < cycle->connection_n; i++) {
            if (c[i].fd != -1
                && c[i].read
                && !c[i].read->accept
                && !c[i].read->channel
                && !c[i].read->resolver)
            {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                              "*%uA open socket #%d left in connection %ui",
                              c[i].number, c[i].fd, i);
                ngx_debug_quit = 1;
            }
        }

        if (ngx_debug_quit) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, 0, "aborting");
            ngx_debug_point();
        }
    }

    /*
     * Copy ngx_cycle->log related data to the special static exit cycle,
     * log, and log file structures enough to allow a signal handler to log.
     * The handler may be called when standard ngx_cycle->log allocated from
     * ngx_cycle->pool is already destroyed.
     */

    ngx_exit_log = *ngx_log_get_file_log(ngx_cycle->log);

    ngx_exit_log_file.fd = ngx_exit_log.file->fd;
    ngx_exit_log.file = &ngx_exit_log_file;
    ngx_exit_log.next = NULL;
    ngx_exit_log.writer = NULL;

    ngx_exit_cycle.log = &ngx_exit_log;
    ngx_exit_cycle.files = ngx_cycle->files;
    ngx_exit_cycle.files_n = ngx_cycle->files_n;
    ngx_cycle = &ngx_exit_cycle;

    ngx_destroy_pool(cycle->pool);

    ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0, "exit");

    exit(0);
}


static void
ngx_channel_handler(ngx_event_t *ev)
{
    ngx_int_t          n;
    ngx_channel_t      ch;
    ngx_connection_t  *c;

    if (ev->timedout) {
        ev->timedout = 0;
        return;
    }

    c = ev->data;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ev->log, 0, "channel handler");

    for ( ;; ) {

        n = ngx_read_channel(c->fd, &ch, sizeof(ngx_channel_t), ev->log);

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0, "channel: %i", n);

        if (n == NGX_ERROR) {

            if (ngx_event_flags & NGX_USE_EPOLL_EVENT) {
                ngx_del_conn(c, 0);
            }

            ngx_close_connection(c);
            return;
        }

        if (ngx_event_flags & NGX_USE_EVENTPORT_EVENT) {
            if (ngx_add_event(ev, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return;
            }
        }

        if (n == NGX_AGAIN) {
            return;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0,
                       "channel command: %ui", ch.command);

        switch (ch.command) {

        case NGX_CMD_QUIT:
            ngx_quit = 1;
            break;

        case NGX_CMD_TERMINATE:
            ngx_terminate = 1;
            break;

        case NGX_CMD_REOPEN:
            ngx_reopen = 1;
            break;

        case NGX_CMD_OPEN_CHANNEL:

            ngx_log_debug3(NGX_LOG_DEBUG_CORE, ev->log, 0,
                           "get channel s:%i pid:%P fd:%d",
                           ch.slot, ch.pid, ch.fd);

            ngx_processes[ch.slot].pid = ch.pid;
            ngx_processes[ch.slot].channel[0] = ch.fd;
            break;

        case NGX_CMD_CLOSE_CHANNEL:

            ngx_log_debug4(NGX_LOG_DEBUG_CORE, ev->log, 0,
                           "close channel s:%i pid:%P our:%P fd:%d",
                           ch.slot, ch.pid, ngx_processes[ch.slot].pid,
                           ngx_processes[ch.slot].channel[0]);

            if (close(ngx_processes[ch.slot].channel[0]) == -1) {
                ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                              "close() channel failed");
            }

            ngx_processes[ch.slot].channel[0] = -1;
            break;
        }
    }
}

/*cache manager��cache loader�ӽ��̵Ĺ���ѭ��*/
static void
ngx_cache_manager_process_cycle(ngx_cycle_t *cycle, void *data)
{
    ngx_cache_manager_ctx_t *ctx = data;

    void         *ident[4];
    ngx_event_t   ev;

    /*
     * Set correct process type since closing listening Unix domain socket
     * in a master process also removes the Unix domain socket file.
     */
    ngx_process = NGX_PROCESS_HELPER;

    ngx_close_listening_sockets(cycle);  //cache manager���̲������˿ڣ��رն˿�

    /* Set a moderate number of connections for a helper process. */
    cycle->connection_n = 512;

    ngx_worker_process_init(cycle, -1);  //���̳�ʼ��

    ngx_memzero(&ev, sizeof(ngx_event_t));
    ev.handler = ctx->handler;  //�����¼���ʱ������
    ev.data = ident;
    ev.log = cycle->log;
    ident[3] = (void *) -1;

    ngx_use_accept_mutex = 0;

    ngx_setproctitle(ctx->name);

    ngx_add_timer(&ev, ctx->delay);  //���¼����뵽��ʱ����

    /*����ѭ��*/
    for ( ;; ) {

        if (ngx_terminate || ngx_quit) {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");
            exit(0);
        }

        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, -1);
        }

        /*������ᴦ��ʱ�Ķ�ʱ���¼������cache manager������Ȥ�¼���ʱ����������Ӧ�Ļص�*/
        ngx_process_events_and_timers(cycle);
    }
}

/*cache manager���̵ĳ�ʱ�¼�������*/
static void
ngx_cache_manager_process_handler(ngx_event_t *ev)
{
    time_t        next, n;
    ngx_uint_t    i;
    ngx_path_t  **path;

    next = 60 * 60;

    /*����manager�ص�����*/
    path = ngx_cycle->paths.elts;
    for (i = 0; i < ngx_cycle->paths.nelts; i++) {

        if (path[i]->manager) {
            n = path[i]->manager(path[i]->data);

            next = (n <= next) ? n : next;

            ngx_time_update();
        }
    }

    if (next == 0) {
        next = 1;
    }

    /*������֮���ֽ����¼����뵽��ʱ���еȴ��´γ�ʱ*/
    ngx_add_timer(ev, next * 1000);
}

/*cache loader���̵ĳ�ʱ�¼�������*/
static void
ngx_cache_loader_process_handler(ngx_event_t *ev)
{
    ngx_uint_t     i;
    ngx_path_t   **path;
    ngx_cycle_t   *cycle;

    cycle = (ngx_cycle_t *) ngx_cycle;

    /*����loader�ص�����*/
    path = cycle->paths.elts;
    for (i = 0; i < cycle->paths.nelts; i++) {

        if (ngx_terminate || ngx_quit) {
            break;
        }

        if (path[i]->loader) {
            path[i]->loader(path[i]->data);
            ngx_time_update();
        }
    }

    /*ִ����֮���ӽ��̾��˳���*/
    exit(0);
}

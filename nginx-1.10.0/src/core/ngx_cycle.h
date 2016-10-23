
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CYCLE_H_INCLUDED_
#define _NGX_CYCLE_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


#ifndef NGX_CYCLE_POOL_SIZE
#define NGX_CYCLE_POOL_SIZE     NGX_DEFAULT_POOL_SIZE
#endif


#define NGX_DEBUG_POINTS_STOP   1
#define NGX_DEBUG_POINTS_ABORT  2


typedef struct ngx_shm_zone_s  ngx_shm_zone_t;

typedef ngx_int_t (*ngx_shm_zone_init_pt) (ngx_shm_zone_t *zone, void *data);

struct ngx_shm_zone_s {
    void                     *data;  // ��Ϊinit�����Ĳ��������ڴ�������
    ngx_shm_t                 shm;   //���������ڴ�Ľṹ��
    ngx_shm_zone_init_pt      init;  //������������slab�����ڴ�غ�����������
    void                     *tag;   //��Ӧ��ngx_shared_memory_add��tag����
    ngx_uint_t                noreuse;  /* unsigned  noreuse:1; */
};


struct ngx_cycle_s {
    /*conf_ctx����������ģ��洢������Ľṹ��ָ��(conf_ctx������Ǹ�����洢��ֻ�к���ģ�������������ṹ��ָ������)*/
    void                  ****conf_ctx;
    ngx_pool_t               *pool;

    ngx_log_t                *log;
    ngx_log_t                 new_log;

    /* error_logָ���Ƿ��ӡ����־��������ı�־λ */
    ngx_uint_t                log_use_stderr;  /* unsigned  log_use_stderr:1; */

    /*
     * ����poll��rtsig�������¼�ģ�飬������Ч�ļ��������Ԥ�ȴ�����Щngx_connection_t�ṹ�壬�Լ����¼����ռ�
     * �ͷַ�����ʱfiles�ͻᱣ��������ngx_connection_t��ָ����ɵ����飬files_n��������Ԫ�ص����������ļ����ֵ
     * ��������files�����Ա
     */
    ngx_connection_t        **files;

    /*free_connections��ʾ�������ӳأ�free_connection_n�������ӳ��������������ʹ��*/
    ngx_connection_t         *free_connections;
    ngx_uint_t                free_connection_n;

    /*modules��ʾnginx��ģ����ɵ����飬������̬����Ͷ�̬���ص�*/
    ngx_module_t            **modules;
     /*ngx_modules_n��ʾ��ǰ��̬������ں˵�ģ������*/
    ngx_uint_t                modules_n;  
    ngx_uint_t                modules_used;    /* unsigned  modules_used:1; */

    /*˫������������Ԫ��������ngx_connection_t�ṹ�壬��ʾ���ظ�ʹ�õ����Ӷ���(������)*/
    ngx_queue_t               reusable_connections_queue;

    /*��̬���飬ÿ��Ԫ�ش洢��ngx_listening_t��Ա����ʾ�����Ķ˿ڼ���ز���*/
    ngx_array_t               listening;

    /*    
     * ��̬������������������nginx����Ҫ������Ŀ¼�������Ŀ¼�����ڣ��ͻ���ͼ������������Ŀ¼ʧ�ܾͻᵼ��nginx����
     * ʧ��.ͨ�����������ļ���ȡ����·����ӵ������飬����nginx.conf�е�client_body_temp_path proxy_temp_path��
     * �ο�ngx_conf_set_path_slot.��Щ���ÿ��������ظ���·������˲���Ҫ�ظ�������ͨ��ngx_add_path�����ӵ�·���Ƿ�
     * �ظ������ظ�����ӵ�paths��
     */
    ngx_array_t               paths;
    ngx_array_t               config_dump;

    /*
     * ������������Ԫ��������ngx_open_file_t�ṹ�壬����ʾNginx���Ѿ��򿪵������ļ�����ʵ�ϣ�Nginx��ܲ�������open_files
     * ������ļ��������ɶԴ˸���Ȥ��ģ������������ļ�·����Nginx��ܻ���ngx_init_cycle()�����д���Щ�ļ�
     */
    ngx_list_t                open_files;

    /*
     * ������������Ԫ��������ngx_shm_zone_t�ṹ�壬ÿ��Ԫ�ر�ʾһ�鹲���ڴ�
     */
    ngx_list_t                shared_memory;

    /*��ǰ�������������Ӷ����������������connections read_events write_events���ʹ��*/
    ngx_uint_t                connection_n;
    ngx_uint_t                files_n;

    /* 
     * connections��ʾ���ӳ�;read_events��ʾ���ж��¼�;write_events��ʾ����д�¼�����������Ԫ��һһ��Ӧ
     * ��һ�����Ӷ�Ӧһ�����¼���һ��д�¼�
     */
    ngx_connection_t         *connections;
    ngx_event_t              *read_events;
    ngx_event_t              *write_events;

    /*��ʱʹ�õ�cycle,���б�����conf_file conf_prefix conf_param prefix����ngx_init_cycle()�н���ת��*/
    ngx_cycle_t              *old_cycle;

    /*�����ļ�����ڰ�װĿ¼��·������ Ĭ��Ϊ��װ·���µ�NGX_CONF_PATH,��ngx_process_options*/
    ngx_str_t                 conf_file;
    ngx_str_t                 conf_param; //nginx���������ļ�ʱ��Ҫ���⴦�����������Я���Ĳ�����һ����-g ѡ��Я���Ĳ���
    ngx_str_t                 conf_prefix; // nginx�����ļ�����Ŀ¼��·��
    ngx_str_t                 prefix;  //nginx��װĿ¼��·��
    ngx_str_t                 lock_file;
    ngx_str_t                 hostname; //ʹ��gethostnameϵͳ���û�õ�������
};


typedef struct {
    ngx_flag_t                daemon;  //�Ƿ��Ѻ�̨��ʽ���б�־λ
    ngx_flag_t                master;  //masterģʽ�ı�־λ

    /*��timer_resolutionȫ�������н������Ĳ�������ʾ���ٸ�ms��ִ�ж�ʱ���жϣ�Ȼ���epoll_wait���ظ����ڴ�ʱ���¼�*/
    ngx_msec_t                timer_resolution;

    ngx_int_t                 worker_processes;  //�����̵߳ĸ���
    ngx_int_t                 debug_points;

    ngx_int_t                 rlimit_nofile;  //�Ƚ��̿��Դ򿪵�����ļ���������1��ֵ
    off_t                     rlimit_core;  //coredump�ļ��Ĵ�С

    int                       priority;  //�������ȼ�

    ngx_uint_t                cpu_affinity_auto;
    /*
     worker_processes 4;
     worker_cpu_affinity 0001 0010 0100 1000; �ĸ��������̷ֱ����ĸ�ָ����he��������
     
     �����5�˿�����������
     worker_cpu_affinity 00001 00010 00100 01000 10000; �����������
     */
    ngx_uint_t                cpu_affinity_n;  //worker_cpu_affinity����Ĳ�������
    ngx_cpuset_t             *cpu_affinity;//worker_cpu_affinity 0001 0010 0100 1000;ת����λͼ�������0x1111

    char                     *username;
    ngx_uid_t                 user;   //�����û�ID
    ngx_gid_t                 group;  //�����û���ID

    ngx_str_t                 working_directory;
    ngx_str_t                 lock_file;

    ngx_str_t                 pid;
    ngx_str_t                 oldpid;

    ngx_array_t               env;
    char                    **environment;  //��������
} ngx_core_conf_t;


#define ngx_is_init_cycle(cycle)  (cycle->conf_ctx == NULL)


ngx_cycle_t *ngx_init_cycle(ngx_cycle_t *old_cycle);
ngx_int_t ngx_create_pidfile(ngx_str_t *name, ngx_log_t *log);
void ngx_delete_pidfile(ngx_cycle_t *cycle);
ngx_int_t ngx_signal_process(ngx_cycle_t *cycle, char *sig);
void ngx_reopen_files(ngx_cycle_t *cycle, ngx_uid_t user);
char **ngx_set_environment(ngx_cycle_t *cycle, ngx_uint_t *last);
ngx_pid_t ngx_exec_new_binary(ngx_cycle_t *cycle, char *const *argv);
ngx_cpuset_t *ngx_get_cpu_affinity(ngx_uint_t n);
ngx_shm_zone_t *ngx_shared_memory_add(ngx_conf_t *cf, ngx_str_t *name,
    size_t size, void *tag);


extern volatile ngx_cycle_t  *ngx_cycle;
extern ngx_array_t            ngx_old_cycles;
extern ngx_module_t           ngx_core_module;
extern ngx_uint_t             ngx_test_config;
extern ngx_uint_t             ngx_dump_config;
extern ngx_uint_t             ngx_quiet_mode;


#endif /* _NGX_CYCLE_H_INCLUDED_ */

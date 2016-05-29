
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>

/*
 * Nginx�����̼乲�����ݵ���Ҫ��ʽ��ʹ�ù����ڴ棬һ����˵�������ڴ�����master���̴�����
 * ��master����fork��worker�ӽ��̺����еĽ��̶���ʼʹ����鹲���ڴ��е�������
 */

//NGX_HAVE_MAP_ANON��ʾ��mmapϵͳ����ʵ�ֻ�ȡ���ͷŹ����ڴ�ķ���
#if (NGX_HAVE_MAP_ANON)

ngx_int_t
ngx_shm_alloc(ngx_shm_t *shm)
{
    /*
     * void *mmap(void *start, size_t length, int prot, int flags, int fd, off_t offset)
     * 1. ��ϵͳ���ÿ��Խ������ļ�ӳ�䵽�ڴ��У����ں�ͬ���ڴ�ʹ����ļ��е�����
     * 2. fd���ļ��������������ʾ����ӳ��Ĵ����ļ�
     * 3. offset��ʾ���ļ������ƫ������ʼ����
     * 4. ��flags�����м���MAP_ANON����MAP_ANONYMOUSʱ��ʾ��ʹ���ļ�ӳ�䷽ʽ����ʱfd��offset������
     * 5. prot��ʾ�����˿鹲���ڴ�ķ�ʽ����PROT_READ����PROT_WRITE��
     * 6. length��ʾ���Ǵ˿鹲���ڴ�ĳ���
     * 7. start��ʾ����ϣ�������ڴ����ʼ��ַ��ͨ����ΪNULL
     */
    shm->addr = (u_char *) mmap(NULL, shm->size,
                                PROT_READ|PROT_WRITE,
                                MAP_ANON|MAP_SHARED, -1, 0);

    if (shm->addr == MAP_FAILED) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "mmap(MAP_ANON|MAP_SHARED, %uz) failed", shm->size);
        return NGX_ERROR;
    }

    return NGX_OK;
}


void
ngx_shm_free(ngx_shm_t *shm)
{
    /*���ӳ�䣬�ͷŹ����ڴ�*/
    if (munmap((void *) shm->addr, shm->size) == -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "munmap(%p, %uz) failed", shm->addr, shm->size);
    }
}

//NGX_HAVE_MAP_DEVZERO��ʾ��/dev/zero�ļ�ʹ��mmapʵ�ֹ����ڴ�
#elif (NGX_HAVE_MAP_DEVZERO)

ngx_int_t
ngx_shm_alloc(ngx_shm_t *shm)
{
    ngx_fd_t  fd;

    fd = open("/dev/zero", O_RDWR);

    if (fd == -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "open(\"/dev/zero\") failed");
        return NGX_ERROR;
    }

    shm->addr = (u_char *) mmap(NULL, shm->size, PROT_READ|PROT_WRITE,
                                MAP_SHARED, fd, 0);

    if (shm->addr == MAP_FAILED) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "mmap(/dev/zero, MAP_SHARED, %uz) failed", shm->size);
    }

    if (close(fd) == -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "close(\"/dev/zero\") failed");
    }

    return (shm->addr == MAP_FAILED) ? NGX_ERROR : NGX_OK;
}


void
ngx_shm_free(ngx_shm_t *shm)
{
    if (munmap((void *) shm->addr, shm->size) == -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "munmap(%p, %uz) failed", shm->addr, shm->size);
    }
}

//NGX_HAVE_SYSVSHM��ʾʹ��shmgetϵͳ������������ͷŹ����ڴ�
#elif (NGX_HAVE_SYSVSHM)

#include <sys/ipc.h>
#include <sys/shm.h>


ngx_int_t
ngx_shm_alloc(ngx_shm_t *shm)
{
    int  id;

    /*
     * int shmget(key_t key, size_t size, int flag);��ȡһ�������ڴ��ʶ������
     *      ����һ�������ڴ���󲢷��ع����ڴ��ʶ��
     * 1.key:��ʾ��ʶ������ size��ʾ�����ڴ�Ĵ�С flag��ʾ��дȨ��
     * 2. key��ʶ�����ڴ�ļ�ֵ: 0/IPC_PRIVATE�� ��key��ȡֵΪIPC_PRIVATE��
     *   ����shmget()������һ���µĹ����ڴ棻���key��ȡֵΪ0��������shmflg��
     *   ������IPC_PRIVATE�����־����ͬ��������һ���µĹ����ڴ档
     * 3.����ֵ���ɹ����ع���洢��id��ʧ�ܷ���-1
     */
    id = shmget(IPC_PRIVATE, shm->size, (SHM_R|SHM_W|IPC_CREAT));

    if (id == -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "shmget(%uz) failed", shm->size);
        return NGX_ERROR;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_CORE, shm->log, 0, "shmget id: %d", id);

    /*
     * void *shmat(int shmid, const void *addr, int flag);�ѹ����ڴ�������ӳ�䵽���ý��̵ĵ�ַ�ռ�
     * 1.shmid��ʾ����洢id
     * 2.addr һ��Ϊ0
     * 3.flag һ��Ϊ0
     * 4.����ֵ������ɹ������ع���洢�ε�ַ��������-1
     */
    shm->addr = shmat(id, NULL, 0);

    if (shm->addr == (void *) -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno, "shmat() failed");
    }

    /*
     * int shmctl(int shmid,int cmd,struct shmid_ds *buf) ��ɶԹ����ڴ�Ŀ���
     * 1.shmid����洢id
     * 2.cmd����������:
     *    1) IPC_STAT:�õ������ڴ��״̬���ѹ����ڳ��е�shmid_ds�ṹ�帴�Ƶ�buf��
     *    2) IPC_SET:�ı乲���ڴ��״̬����buf�е�uid��gid��mode�ȸ��Ƶ������ڴ��shmid_ds��
     *    3) IPC_RMID:ɾ����鹲���ڴ�
     */
    if (shmctl(id, IPC_RMID, NULL) == -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "shmctl(IPC_RMID) failed");
    }

    return (shm->addr == (void *) -1) ? NGX_ERROR : NGX_OK;
}


void
ngx_shm_free(ngx_shm_t *shm)
{
    /*
     * int shmdt(const void *shmaddr)
     * �����Ͽ��빲���ڴ�����ӣ���ֹ�����̷�����鹲���ڴ棬������ɾ��shmaddrָ��Ĺ����ڴ�
     */
    if (shmdt(shm->addr) == -1) {
        ngx_log_error(NGX_LOG_ALERT, shm->log, ngx_errno,
                      "shmdt(%p) failed", shm->addr);
    }
}

#endif

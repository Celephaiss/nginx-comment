
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SHMTX_H_INCLUDED_
#define _NGX_SHMTX_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct {
    ngx_atomic_t   lock;
#if (NGX_HAVE_POSIX_SEM)
    ngx_atomic_t   wait;
#endif
} ngx_shmtx_sh_t;


typedef struct {
#if (NGX_HAVE_ATOMIC_OPS)     //��ԭ�ӱ���ʵ�ֻ���������ָ�����һ�ι����ڴ�ռ䣬Ϊ0��ʾ���Ի����
    ngx_atomic_t  *lock;      //ԭ�ӱ�����
#if (NGX_HAVE_POSIX_SEM)      //֧���ź���
    ngx_atomic_t  *wait;      //��ʾ�ȴ���ȡԭ�ӱ�������ʹ�õ��ź���������(��ʱ���Ǻ����)
    ngx_uint_t     semaphore; //semaphoreΪ1��ʾ��ȡ��ʱ����ʹ�õ��ź���
    sem_t          sem;       //�ź�����
#endif
#else                         //���ļ���ʵ�ֻ�����
    ngx_fd_t       fd;        //��ʾ�ļ����
    u_char        *name;      //�ļ���
#endif
    //������������ʾ������״̬�µȴ�����������ִ�н�����ͷ�����ʱ��(���ڶദ����״̬�²�������)��
    //���ļ���ʵ��ʱ�����壬spinֵΪ-1���Ǹ���Nginx����������ý��̽���˯��״̬
    ngx_uint_t     spin;
} ngx_shmtx_t;


ngx_int_t ngx_shmtx_create(ngx_shmtx_t *mtx, ngx_shmtx_sh_t *addr,
    u_char *name);
void ngx_shmtx_destroy(ngx_shmtx_t *mtx);
ngx_uint_t ngx_shmtx_trylock(ngx_shmtx_t *mtx);
void ngx_shmtx_lock(ngx_shmtx_t *mtx);
void ngx_shmtx_unlock(ngx_shmtx_t *mtx);
ngx_uint_t ngx_shmtx_force_unlock(ngx_shmtx_t *mtx, ngx_pid_t pid);


#endif /* _NGX_SHMTX_H_INCLUDED_ */

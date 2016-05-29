
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_SLAB_H_INCLUDED_
#define _NGX_SLAB_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>


typedef struct ngx_slab_page_s  ngx_slab_page_t;

struct ngx_slab_page_s {
    uintptr_t         slab;  //����;������ҳ�����Ϣ��bitmap���ڴ���С
    ngx_slab_page_t  *next;  //ָ��˫�������е���һ��ҳ
    uintptr_t         prev;  //ָ��˫������ǰһ��ҳ����2λ���ڴ���ڴ������
};


typedef struct {
    ngx_shmtx_sh_t    lock;      

    size_t            min_size;  //һҳ����С�ڴ��(chunk)��С
    size_t            min_shift; //һҳ����С�ڴ���Ӧ��ƫ��

    ngx_slab_page_t  *pages;     //slab�ڴ��������ҳ������
    ngx_slab_page_t  *last;      //ָ�����һ������ҳ
    ngx_slab_page_t   free;      //�ڴ���п���ҳ�������ͷ��

    u_char           *start;     //ʵ��ҳ��ʼ��ַ
    u_char           *end;       //ʵ��ҳ������ַ

    ngx_shmtx_t       mutex;     //slab�ڴ�ػ�����

    u_char           *log_ctx;
    u_char            zero;

    unsigned          log_nomem:1;

    void             *data;     
    void             *addr;      //ָ���ڴ����ʼ��ַ
} ngx_slab_pool_t;


void ngx_slab_init(ngx_slab_pool_t *pool);
void *ngx_slab_alloc(ngx_slab_pool_t *pool, size_t size);
void *ngx_slab_alloc_locked(ngx_slab_pool_t *pool, size_t size);
void *ngx_slab_calloc(ngx_slab_pool_t *pool, size_t size);
void *ngx_slab_calloc_locked(ngx_slab_pool_t *pool, size_t size);
void ngx_slab_free(ngx_slab_pool_t *pool, void *p);
void ngx_slab_free_locked(ngx_slab_pool_t *pool, void *p);


#endif /* _NGX_SLAB_H_INCLUDED_ */

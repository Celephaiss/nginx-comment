
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */

#include <ngx_config.h>
#include <ngx_core.h>


#define NGX_SLAB_PAGE_MASK   3
#define NGX_SLAB_PAGE        0
#define NGX_SLAB_BIG         1
#define NGX_SLAB_EXACT       2
#define NGX_SLAB_SMALL       3

#if (NGX_PTR_SIZE == 4)

#define NGX_SLAB_PAGE_FREE   0
#define NGX_SLAB_PAGE_BUSY   0xffffffff
#define NGX_SLAB_PAGE_START  0x80000000

#define NGX_SLAB_SHIFT_MASK  0x0000000f
#define NGX_SLAB_MAP_MASK    0xffff0000
#define NGX_SLAB_MAP_SHIFT   16

#define NGX_SLAB_BUSY        0xffffffff

#else /* (NGX_PTR_SIZE == 8) */

#define NGX_SLAB_PAGE_FREE   0
#define NGX_SLAB_PAGE_BUSY   0xffffffffffffffff
#define NGX_SLAB_PAGE_START  0x8000000000000000

#define NGX_SLAB_SHIFT_MASK  0x000000000000000f
#define NGX_SLAB_MAP_MASK    0xffffffff00000000
#define NGX_SLAB_MAP_SHIFT   32

#define NGX_SLAB_BUSY        0xffffffffffffffff

#endif


#if (NGX_DEBUG_MALLOC)

#define ngx_slab_junk(p, size)     ngx_memset(p, 0xA5, size)

#elif (NGX_HAVE_DEBUG_MALLOC)

#define ngx_slab_junk(p, size)                                                \
    if (ngx_debug_malloc)          ngx_memset(p, 0xA5, size)

#else

#define ngx_slab_junk(p, size)

#endif

static ngx_slab_page_t *ngx_slab_alloc_pages(ngx_slab_pool_t *pool,
    ngx_uint_t pages);
static void ngx_slab_free_pages(ngx_slab_pool_t *pool, ngx_slab_page_t *page,
    ngx_uint_t pages);
static void ngx_slab_error(ngx_slab_pool_t *pool, ngx_uint_t level,
    char *text);

/*
 * 1. ngx_slab_max_size ��ʾһҳ������ֽ���,Ϊngx_slab_max_size = ngx_pagesize / 2
 * 2. ngx_slab_max_size ��ʾһҳ�Ļ�׼�ֽ���,Ϊngx_slab_max_size = 128 Bytes
 * 3. ngx_slab_exact_shift ��ʾһҳ��׼�ֽ�����Ӧ��λ��,ngx_slab_exact_shift = 7
 * 4. ngx_slab_max_size = 128 ��Դ����һ��ָ�볤�ȸպÿ�����λͼ����ʾһҳ�е�ÿ��
 *    �ڴ���Ƿ���ʹ��
 */
static ngx_uint_t  ngx_slab_max_size;
static ngx_uint_t  ngx_slab_exact_size;
static ngx_uint_t  ngx_slab_exact_shift;

/*
 * ngx_slab_init����������ʼ��slab�ڴ�أ���Ҫ������������:
 * 1. Ϊ��������ȫ�ֱ�����ֵ
 * 2. ��ʼ��slots���顢pages����
 * 3. ��slab�ڴ�ع���ṹngx_slab_pool_t��س�Ա��ֵ
 */
void
ngx_slab_init(ngx_slab_pool_t *pool)
{
    u_char           *p;
    size_t            size;
    ngx_int_t         m;
    ngx_uint_t        i, n, pages;
    ngx_slab_page_t  *slots;

    /* STUB */
    /*
     * 1.ngx_slab_exact_size��һ����׼ֵ��������Ӧ��һҳ�е��ڴ�������պÿ�����
     *   uintptr_tָ�������λ����ʾ(8 * sizeof(uintptr_t)��ʾ)uintptr_t���еĶ�����λ����
     *   ngx_pagesize / (8 * sizeof(uintptr_t))����ʾ����Ӧ���ڴ��(chunk)�ĳ���
     * 2.��slab�ڴ���У����������ڴ������Ӧ��λ�ƴ�С��ͨ�����ڼ����ڴ���С��
     *   ��λ���ִ�С���ڴ���Ӧ��slot�����Ԫ��
     */
    if (ngx_slab_max_size == 0) {
        ngx_slab_max_size = ngx_pagesize / 2;
        ngx_slab_exact_size = ngx_pagesize / (8 * sizeof(uintptr_t));
        for (n = ngx_slab_exact_size; n >>= 1; ngx_slab_exact_shift++) {
            /* void */
        }
    }
    /**/

    /*
     * min_size��ʾ����slab�ڴ����һҳ�ڵ���С�ڴ��(chunk)��С
     * Ŀǰ�汾��min_shift = 3������ʾ��С�ڴ���СΪ 2 ^ 3 = 8 Bytes
     */
    pool->min_size = 1 << pool->min_shift;

    /*��slab�ڴ��ƫ��sizeof(ngx_slab_pool_t)��С����ʱpָ���slots�����׵�ַ*/
    p = (u_char *) pool + sizeof(ngx_slab_pool_t);
    size = pool->end - p;

    ngx_slab_junk(p, size);

    /*
     * ÿ���ڴ���С��slots�����ж�����һ��Ԫ����֮��Ӧ
     * ngx_pagesize_shift��Ӧ����ҳ��ƫ�ƣ�min_shift��Ӧ������С�ڴ���ƫ��
     * ngx_pagesize_shift - pool->min_shift ��ʾslab�ڴ���а������ڴ�������
     */
    slots = (ngx_slab_page_t *) p;
    n = ngx_pagesize_shift - pool->min_shift;

    for (i = 0; i < n; i++) {
        slots[i].slab = 0;
        slots[i].next = &slots[i];  //��ʼ����ʱ��slots����Ԫ��ָ��������ʾ��
        slots[i].prev = 0;
    }

    /*p��slots����Ļ�����ƫ��n * sizeof(ngx_slab_page_t)��ָ��pages����*/
    p += n * sizeof(ngx_slab_page_t);

    /*
     * ���ڳ�������slab�ڴ��к��е�4kҳ������,���ں���ÿҳ����ʼ��ַҪ��4k���룬
     * ���Զ��������в����ڴ�ռ���˷ѣ�����ʵ�ʵ�ҳ������������������
     * ��������и����ʣ������������size��С������slots����ĳ��Ȼ᲻���׼ȷЩ?
     * ��size = pool->end -p;�����������ǲ��ǻ��׼ȷЩ(��ʵ����ļ����pages��
     * �����ַ���������¼���)
     */
    pages = (ngx_uint_t) (size / (ngx_pagesize + sizeof(ngx_slab_page_t)));

    ngx_memzero(p, pages * sizeof(ngx_slab_page_t));

    /*��ʼ��slab�ڴ����pages������׵�ַ*/
    pool->pages = (ngx_slab_page_t *) p;

    /*
     * free����ָ���ڴ���еĿ���ҳ��ɵ�������ʼ�����ָ��pages�����׵�ַ��
     * ��ʾ����ҳ���ǿ��еģ�pool->free.nextָ���´δ��ڴ���з���ҳ�ĵ�ַ
     */
    pool->free.prev = 0;
    pool->free.next = (ngx_slab_page_t *) p;

    /*
     * ��ʼ�����pool->pages������һ���ڴ�ҳ�е�slab��ʾ��������������ʣ��ҳ����Ŀ
     * pool->pages->next��pool->pages->prev��ָ��pool->free,��������˫������
     */
    pool->pages->slab = pages;
    pool->pages->next = &pool->free;
    pool->pages->prev = (uintptr_t) &pool->free;

    /*
     * pool->startָ��slab�ڴ�������ʵ�ʷ�����û����ڴ���׵�ַ�������׵�ַ��Ҫ��֤
     * 4k����ģ�֮����Ҫ4k���룬�Ƿ������pages�����ʵ�ʶ�Ӧ�����ڷ����ҳ����ͨ��
     * ƫ�������й���������ҳ���ڴ����������ͷ�
     */
    pool->start = (u_char *)
                  ngx_align_ptr((uintptr_t) p + pages * sizeof(ngx_slab_page_t),
                                 ngx_pagesize);

    /*
     * ������ö���֮��ĵ�ַ������ڷ���ҳ���ܴ�С������ҳ��Сngx_pagesize
     * �������slab�ڴ���ʵ�ʰ�����ҳ������
     */
    m = pages - (pool->end - pool->start) / ngx_pagesize;
    if (m > 0) {
        pages -= m;
        pool->pages->slab = pages;
    }

    /*
     * pool->lastָ��pages������ĩβ�������һҳ��ĵ�ַ����ʵpages��������������ҳ�ģ�
     * ������һһ��Ӧ�ģ���ÿ��ҳ����һ��ngx_slab_page_t����ṹ
     */
    pool->last = pool->pages + pages;

    pool->log_nomem = 1;
    pool->log_ctx = &pool->zero;
    pool->zero = '\0';
}

/*
 * ͨ��Ҫ�õ�slab�ڴ�صĶ��ǿ���̼�ͨ�ŵĳ��������ngx_slab_alloc_locked��
 * ngx_slab_free_locked��Բ������������ڴ������ͷŷ�������ʹ�ã�����ģ����
 * �Ѿ�������ͬ��������ʹ��
 */

/*�����ڴ棬���̼���Ҫ����������ͬ���������Ĺ����ڴ���䷽��*/
void *
ngx_slab_alloc(ngx_slab_pool_t *pool, size_t size)
{
    void  *p;

    /*��������ʽ��ȡ��*/
    ngx_shmtx_lock(&pool->mutex);

    p = ngx_slab_alloc_locked(pool, size);

    ngx_shmtx_unlock(&pool->mutex);

    return p;
}


/*���������ڴ���䷽��*/
void *
ngx_slab_alloc_locked(ngx_slab_pool_t *pool, size_t size)
{
    size_t            s;
    uintptr_t         p, n, m, mask, *bitmap;
    ngx_uint_t        i, slot, shift, map;
    ngx_slab_page_t  *page, *prev, *slots;

    /*
     * ���Ҫ������ڴ����ngx_slab_max_size(ngx_pagesize/2),��˵����Ҫ������ڴ�
     * ����ҪΪһ��ҳ�Ź���������size��С���ڴ��
     */
    if (size > ngx_slab_max_size) {

        ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                       "slab alloc: %uz", size);

        /*
         * (size >> ngx_pagesize_shift)+ ((size % ngx_pagesize) ? 1 : 0)��ʾ�˴�
         * ��Ҫ�����ҳ�����������ʣ�಻��һҳ����Ҫ����һ��ҳ
         */
        page = ngx_slab_alloc_pages(pool, (size >> ngx_pagesize_shift)
                                          + ((size % ngx_pagesize) ? 1 : 0));
        if (page) {
            /*
             * 1.���Ȼ�ȡ�������ҳ��Ӧ��pageԪ�������pages�����׵�ַ��ƫ��
             * 2.Ȼ����ƫ��������pool->start����ָ�����������ڷ��������ҳ�׵�ַ
             */
            p = (page - pool->pages) << ngx_pagesize_shift;
            p += (uintptr_t) pool->start;

        } else {
            p = 0;
        }

        goto done;
    }

    /*
     * 1.���������ڴ�С��ngx_slab_max_size���Ǵ���min_size���������Ҫ������ڴ��(chunk)
     * ��С��Ӧ��ƫ������������sizeΪ54bytes����ʵ������ڴ���СӦ��Ϊ64bytes�������ȡ����
     * 64bytes��Ӧ��ƫ����shift,Ȼ����shift-min_shift��λ�ڴ���Ӧ��slot�����±ָ꣬���Ӧ
     * �ڴ���С�İ���ҳ����Ȼ��Ӱ���ҳ�����������ڴ��
     * 2.��������sizeС���ں�֧�ֵ���С�ڴ���С������С�ڴ������ڴ�����
     */
    if (size > pool->min_size) {
        shift = 1;
        for (s = size - 1; s >>= 1; shift++) { /* void */ }
        slot = shift - pool->min_shift;

    } else {
        size = pool->min_size;
        shift = pool->min_shift;
        slot = 0;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                   "slab alloc: %uz slot: %ui", size, slot);

    /*��ȡslot������׵�ַ����������ȡ���Ĵ˴δ������ڴ���Ӧ���±��ȡ����ҳ����*/
    slots = (ngx_slab_page_t *) ((u_char *) pool + sizeof(ngx_slab_pool_t));
    page = slots[slot].next;  //slots[slot].nextָ���´η����ڴ���׸�����Ԫ��

    /*
     * page->next != page������ǰ�Ѿ��������shift��Ӧ���ڴ�飬��������ʣ���chunk
     * �������ڷ���(��Ϊȫ��ҳ����������)
     * ���⣬�ڳ�ʼ��������������������ʱû��Ԫ�أ�slots[slot].nextָ������
     */
    if (page->next != page) {

        /*���������ڴ�С��128bytes�������÷�֧*/
        if (shift < ngx_slab_exact_shift) {

            do {
                /*
                 * ����size < ngx_slab_exact_shift������һҳ�����ܾ��ֵ��ڴ�����������
                 * uintptr_t���͵�λ���������Ҫ��ҳ��ʵ�����ڷ�����ڴ�����洢����
                 * ָʾĳ���ڴ���Ƿ�ʹ�õı�־��ϣ���bitmap
                 * ��Ҫע����ǣ����ڴ��bitmap���ڴ������ҳ���׵�ַ��ʼ�����
                 * ����nginx�ں�֧�ֵķ�����ڴ���С�������ڴ洢bitmap������ڴ��������������:
                 * (1 << (ngx_pagesize_shift - shift)) / 8 / (1 << shift),����һ����һ������
                 */
                p = (page - pool->pages) << ngx_pagesize_shift;
                bitmap = (uintptr_t *) (pool->start + p);

                /*�������ڴ洢��ʾ��Ӧ�ڴ���Ƿ�ʹ�õ�bitmap������*/
                map = (1 << (ngx_pagesize_shift - shift))
                          / (sizeof(uintptr_t) * 8);

                /*����bitmap�����ڻ�ȡ�����ڷ�����ڴ��*/
                for (n = 0; n < map; n++) {

                    /*bitmap[n] != NGX_SLAB_BUSY��ʾ��n��bitmap�п����ڴ������ڷ���*/
                    if (bitmap[n] != NGX_SLAB_BUSY) {

                        /*i��ʼ���ڱ�ʾ���뵽���ڴ����bitmap�е�λ��*/
                        for (m = 1, i = 0; m; m <<= 1, i++) {

                            /*���ҵ����ȡ��һ���ɷ�����ڴ��*/
                            if ((bitmap[n] & m)) {
                                continue;
                            }

                            /*���˴λ�ȡ���ڴ����ʹ�ñ�־λ��1����ʾ��ʹ�ã��´β������˿�*/
                            bitmap[n] |= m;

                            /*��ʱi��ʾ���Ǵ˴����뵽���ڴ�������ҳ�׵�ַ��ƫ����*/
                            i = ((n * sizeof(uintptr_t) * 8) << shift)
                                + (i << shift);

                            /*
                             * �����֧�����ж�����ҳ�Ƿ��Ѿ����޿ɷ����ڴ�飬
                             * ���û�У�˵����ҳ�Ѿ��Ӱ���ҳ��Ϊ��ȫ��ҳ����Ҫ����
                             * ���ڰ���ҳ����
                             */
                            if (bitmap[n] == NGX_SLAB_BUSY) {
                                for (n = n + 1; n < map; n++) {
                                    if (bitmap[n] != NGX_SLAB_BUSY) {
                                        p = (uintptr_t) bitmap + i;

                                        goto done;
                                    }
                                }

                                /*
                                 * �������ִ�е����˵����ҳ����ȫ��ҳ
                                 * ngx_slab_page_t�е�prev��������ָ�������е���һ��Ԫ���⣬��
                                 * �����λ����ָʾ����ҳ���ڴ�������
                                 */
                                prev = (ngx_slab_page_t *)
                                            (page->prev & ~NGX_SLAB_PAGE_MASK);
                                prev->next = page->next;
                                page->next->prev = page->prev;

                                /*
                                 * ȫ��ҳ��prevָ�벻ָ���κζ����������ڴ洢ָʾ�����ڴ�������ı�־
                                 * nextָ��Ҳ��ָ���κζ�������ΪNULL
                                 */
                                page->next = NULL;
                                page->prev = NGX_SLAB_SMALL; 
                            }

                            /*��λ�˴η�����ڴ��ĵ�ַ*/
                            p = (uintptr_t) bitmap + i;

                            goto done;
                        }
                    }
                }

                page = page->next;

            } while (page);

        } else if (shift == ngx_slab_exact_shift) { //����Ҫ������ڴ���СΪ128bytes

            do {
                /*
                 * ���ڴ�СΪngx_slab_exact_size���ڴ�飬��ҳ����ṹ�е�slab���ڱ�ʾbitmap
                 * �����ڱ�ʾ��Ӧ�ڴ���Ƿ���ʹ��
                 */
                if (page->slab != NGX_SLAB_BUSY) {

                    for (m = 1, i = 0; m; m <<= 1, i++) {
                        if ((page->slab & m)) {
                            continue;
                        }

                        page->slab |= m;  //��Ӧbitmapλ��λ

                        /*�ж϶�Ӧ�İ���ҳ�Ƿ��˻���ȫ��ҳ������ǣ����������ҳ����*/
                        if (page->slab == NGX_SLAB_BUSY) {
                            prev = (ngx_slab_page_t *)
                                            (page->prev & ~NGX_SLAB_PAGE_MASK);
                            prev->next = page->next;
                            page->next->prev = page->prev;

                            page->next = NULL;
                            page->prev = NGX_SLAB_EXACT;
                        }

                        /*����page��pages�����ƫ�ƻ�ȡ������ڴ������ҳ����start��ƫ��*/
                        p = (page - pool->pages) << ngx_pagesize_shift;
                        p += i << shift; //��ȡ������ڴ�������ҳ��ƫ����
                        p += (uintptr_t) pool->start;

                        goto done;
                    }
                }

                page = page->next;

            } while (page);

        } else { /* shift > ngx_slab_exact_shift */

            /*
             * ��ʱ������ڴ��Ĵ�С����(ngx_slab_exact_size,ngx_slab_max_slab)������
             * ��Ϊ�ڴ����128bytes����������Ҫ����ָʾ�ڴ���Ƿ�ʹ�õ�λ��С��32λ��Ҳ����˵
             * һ��uintptr_t���͵ı����������ڱ�ʾbitmap��ʣ���λ��������ʾ�ڴ���С�����
             * ngx_slab_page_tҳ����ṹ���е�slab��16λ������ʾbitmap������λ������ʾ�ڴ��
             * ��С��Ӧ��ƫ�ƣ�����λ���Ա�ʾ���ڴ���Сƫ������
             */
             
            /*
             * page->slab & NGX_SLAB_SHIFT_MASK��Ϊ�ڴ���С��Ӧ��ƫ��
             * 1 << (page->slab & NGX_SLAB_SHIFT_MASK)��Ϊһҳ�ڴ��ж�Ӧƫ��Ϊshift���ڴ��
             * ������
             */ 
            n = ngx_pagesize_shift - (page->slab & NGX_SLAB_SHIFT_MASK);
            n = 1 << n;
            n = ((uintptr_t) 1 << n) - 1;   //��ȡ�ڴ��������Ӧ������
            mask = n << NGX_SLAB_MAP_SHIFT; //��Ϊ��16λ����������ʾbitmap�ģ�������Ҫ����16λ

            do {
                /*page->slab & NGX_SLAB_MAP_MASK��ȡ�ڴ��ʹ����������ֵΪ��Ϊmask��˵����ҳ��Ȼ�ǰ���ҳ*/
                if ((page->slab & NGX_SLAB_MAP_MASK) != mask) {

                    /*
                     * ��ѭ����bitmap�����λ��ʼ�ж϶�Ӧ���ڴ���Ƿ�ʹ��
                     * page->slab & mΪ1��ʾ��Ӧ�ڴ���Ѿ�ʹ�ã��������±���
                     * i��ʾ������������ڴ��������ҳ�ĵڼ����ڴ��
                     */
                    for (m = (uintptr_t) 1 << NGX_SLAB_MAP_SHIFT, i = 0;
                         m & mask;
                         m <<= 1, i++)
                    {
                        if ((page->slab & m)) {
                            continue;
                        }

                        /*�ҵ���һ������ڴ棬bitmapλ��1*/
                        page->slab |= m;

                        /*page->slab & NGX_SLAB_MAP_MASK) == mask������ҳ��������Ҫ�������ҳ����*/
                        if ((page->slab & NGX_SLAB_MAP_MASK) == mask) {
                            prev = (ngx_slab_page_t *)
                                            (page->prev & ~NGX_SLAB_PAGE_MASK); //prev�����λ������ʾ�ڴ�����࣬��ȡָ���ʱ����Ҫ��������λ
                            prev->next = page->next;
                            page->next->prev = page->prev;

                            /*����ȫ��ҳ��prev��next������ָ���κζ�����next��ΪNULL��prev��Ϊ�ڴ�������ֵ*/
                            page->next = NULL;
                            page->prev = NGX_SLAB_BIG;
                        }

                        /*
                         * 1.���������ڴ������ҳ��Ӧ��ҳ�׵�ƫ����(ʵ�����ڷ��������ҳ�Ķ�Ӧ��ƫ��)
                         * 2.�������뵽���ڴ�������ҳ��ƫ����
                         * 3.�������뵽���ڴ��ľ��Ե�ַ
                         */
                        p = (page - pool->pages) << ngx_pagesize_shift;
                        p += i << shift;
                        p += (uintptr_t) pool->start;

                        goto done;
                    }
                }

                page = page->next;

            } while (page);
        }
    }

    /*
     * �������ִ�е��������size����Ӧ�İ���ҳ�����в����ڿɷ�����ڴ��ҳ
     * ����֮ǰ��Ӧ��ƫ��Ϊshift�İ���ҳ��������Ԫ�أ�����ȫ��ҳ���뵼�������� ����
     * ����֮ǰnginx�ں˾�û��Ϊƫ��Ϊshift��С���ڴ������ҳ����������Ϊ��
     * �����������ֿ������������Ҫ��������һ��ҳ�����������Ӧ��ƫ��Ϊshift���ڴ��
     */
    page = ngx_slab_alloc_pages(pool, 1);

    if (page) {

        /*shiftƫ����С�ڻ�׼ƫ����(7)*/
        if (shift < ngx_slab_exact_shift) {
            p = (page - pool->pages) << ngx_pagesize_shift; //���������ҳ�����ҳ�׵�ƫ����
            bitmap = (uintptr_t *) (pool->start + p);  //�������������֪���������ڴ���СС�ڻ�׼�ڴ���Сʱ����Ҫʹ���ڴ�����洢bitmap

            s = 1 << shift;  //�ڴ���С
            
            /* 
             * (1 << (ngx_pagesize_shift - shift)) / 8 �������bitmap��Ҫռ���ֽ���,
             * �ٳ���һ���ڴ���С���õ�bitmap��Ҫռ�ö��ٸ��ڴ�飬����һ����һ������
             */
            n = (1 << (ngx_pagesize_shift - shift)) / 8 / s;  
            if (n == 0) {
                n = 1;
            }

            bitmap[0] = (2 << n) - 1; //�����ڴ洢bitmap���ڴ���Ӧ��bitmapλ��Ϊ1����ʾ�ڴ����ʹ��

            /*mapֵ����ʾ�ж��ٸ�bitmap�� sizeof(uintptr_t) * 8��ʾһ��bitmap����ָʾ���ڴ������*/
            map = (1 << (ngx_pagesize_shift - shift)) / (sizeof(uintptr_t) * 8);

            for (i = 1; i < map; i++) {
                bitmap[i] = 0; //δʹ���ڴ���Ӧbitmapλ��ʼ��Ϊ0
            }

            /*shift < ngx_slab_exact_shiftʱ��page->slab���ڱ�ʾ�ڴ���С*/
            page->slab = shift;
            /*���뵽��ӦslotsԪ��������ײ���Ŀǰֻ��һ��ҳ������page->nextָ������ͷ�� [ͷ�����ײ���һ��]*/
            page->next = &slots[slot];
            page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_SMALL;

            slots[slot].next = page;

            /*
             * ((page - pool->pages) << ngx_pagesize_shift)��ʾ��ҳ��Ӧ����ҳ��ƫ����
             * s * n��ʾ�����ڴ���������ҳ��ƫ����(��ʱ���ǿ����ڷ���ĵ�һ������ҳ�������������ڴ��bitmap��ҳ֮���ҳ)
             */
            p = ((page - pool->pages) << ngx_pagesize_shift) + s * n;
            p += (uintptr_t) pool->start;

            goto done;

        } else if (shift == ngx_slab_exact_shift) {

            /*
             * �����ڴ���ƫ�Ƹպ�Ϊngx_slab_exact_shift,��ʱslab��ʾ����bitmap
             */
            page->slab = 1;

            /*��������slots����İ���ҳ������ײ�*/
            page->next = &slots[slot];
            page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_EXACT;

            slots[slot].next = page;

            /*��ȡ�������ҳ����ҳ�׵�ƫ������Ȼ���ȡ����ҳ�ľ��Ե�ַ��Ҳ�ǵ�һ�η�����ڴ��ĵ�ַ*/
            p = (page - pool->pages) << ngx_pagesize_shift;
            p += (uintptr_t) pool->start;

            goto done;

        } else { /* shift > ngx_slab_exact_shift */

            /*
             * ((uintptr_t) 1 << NGX_SLAB_MAP_SHIFT)�����ڱ�ʾbitmap�ĵ�һλ��1����ʾ��Ӧ�ڴ����ʹ��,
             * ����shift��λȡ�򣬽���ʾ�ڴ��Ĵ�С��λ�Ʒŵ�slab�ĵ���λ��
             */
            page->slab = ((uintptr_t) 1 << NGX_SLAB_MAP_SHIFT) | shift;

            /*�����Ӧ��slots������ɵİ���ҳ������ײ�*/
            page->next = &slots[slot];
            page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_BIG;

            slots[slot].next = page;

            /* 
             * ���㲢������������ڴ��ĵ�ַ(�״����뼴Ϊҳ���׵�ַ��
             * �����NGX_SLAB_SMALL���״������ʱ���ڴ���ַ������ҳ�׵�ַ,��Ϊҳ�׵�ַ�����ڴ���ŵ���bitmap)
             */
            p = (page - pool->pages) << ngx_pagesize_shift;
            p += (uintptr_t) pool->start;

            goto done;
        }
    }

    p = 0;

done:

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0,
                   "slab alloc: %p", (void *) p);

    return (void *) p;
}

/*
 * ngx_slab_calloc����������ngx_slab_alloc���˸��������
 */
void *
ngx_slab_calloc(ngx_slab_pool_t *pool, size_t size)
{
    void  *p;

    ngx_shmtx_lock(&pool->mutex);

    p = ngx_slab_calloc_locked(pool, size);

    ngx_shmtx_unlock(&pool->mutex);

    return p;
}

/*ngx_slab_calloc_locked����������ngx_slab_alloc_locked���˸��������*/
void *
ngx_slab_calloc_locked(ngx_slab_pool_t *pool, size_t size)
{
    void  *p;

    p = ngx_slab_alloc_locked(pool, size);
    if (p) {
        ngx_memzero(p, size);
    }

    return p;
}

/*�������ڴ��ͷŷ���*/
void
ngx_slab_free(ngx_slab_pool_t *pool, void *p)
{
    ngx_shmtx_lock(&pool->mutex);

    ngx_slab_free_locked(pool, p);

    ngx_shmtx_unlock(&pool->mutex);
}


/*�������������ڴ��ͷŷ���*/
/*pָ����Ǵ������ڴ���׵�ַ*/
void
ngx_slab_free_locked(ngx_slab_pool_t *pool, void *p)
{
    size_t            size;
    uintptr_t         slab, m, *bitmap;
    ngx_uint_t        n, type, slot, shift, map;
    ngx_slab_page_t  *slots, *page;

    ngx_log_debug1(NGX_LOG_DEBUG_ALLOC, ngx_cycle->log, 0, "slab free: %p", p);

    /*����У����Ҫ�ͷŵ��ڴ��Ƿ���slab�ڴ����*/
    if ((u_char *) p < pool->start || (u_char *) p > pool->end) {
        ngx_slab_error(pool, NGX_LOG_ALERT, "ngx_slab_free(): outside of pool");
        goto fail;
    }

    /*
     * 1.���ȼ�����ͷ��ڴ���׵�ַ��Ӧ��pages�����е�Ԫ��,����ȡ��Ӧ���ڴ�����ṹ
     * 2.��ȡpage->slab�����ں����ȡҳ�ж�Ӧ�ڴ���һЩ��Ϣ����ϸ������
     * 3.���ڻ�ȡ��ҳ��Ӧ��ŵ��ڴ�������(BIG,EXACT,SMALL,PAGE)
     */
    n = ((u_char *) p - pool->start) >> ngx_pagesize_shift; //����ƫ����
    page = &pool->pages[n];
    slab = page->slab;
    type = page->prev & NGX_SLAB_PAGE_MASK; 

    switch (type) {

    case NGX_SLAB_SMALL:

        /*
         * 1.����NGX_SLAB_SMALL,��bitmap����ڿ�ʼ�����ڴ��chunk�У������Ӧ���ڴ��
         *   ��С��ƫ�������Ƿ���slab�ĵĺ���λ
         * 2.�����ڴ��Ĵ�С
         */
        shift = slab & NGX_SLAB_SHIFT_MASK;
        size = 1 << shift;

        /*
         * ��Ϊ����ʵ�ʷ���ҳ��ҳ�׵�ַ��4k����ģ�����ÿ��ҳ��С��4k������ÿ��ҳ��ҳ��ַ����4k�����
         * ��Ϊ���пɷ�����ڴ��ֻ��8bytes,16bytes,32bytes,...,2048bytes,����ÿ���ڴ��ĵ�ַ���ڸ��ڴ�
         * ���С��˵���Ƕ����
         */
        if ((uintptr_t) p & (size - 1)) {
            goto wrong_chunk;
        }

        /*
         * 1. p & (ngx_pagesize - 1)��ȡ���Ǹô��ͷ��ڴ��(chunk)���������ҳ���׵�ַ��
         *   ƫ����(ȡ��15λ),������shiftλ�������������Ǵ��ͷ��ڴ���ڸ�ҳ���ǵڼ����ڴ��
         * 2. n & (sizeof(uintptr_t) * 8 - 1)ȡ������������Ǹ��ڴ����bitmap��������λ�ã�
             Ȼ�󽫸��ڴ��������bitmap�Ķ�Ӧ��λ��1�� ����n=37,�� 37&31=5,1<<5��bitmap��Ӧλ��1
         * 3.n /= (sizeof(uintptr_t) * 8)������Ǹ��ڴ�����Ǹ�bitmap��
         * 4. (uintptr_t) p & ~((uintptr_t) ngx_pagesize - 1)������Ǹ��ڴ������ҳ���׵�ַ��
             Ҳ����bitmap���׵�ַ
         */
        n = ((uintptr_t) p & (ngx_pagesize - 1)) >> shift;
        m = (uintptr_t) 1 << (n & (sizeof(uintptr_t) * 8 - 1));
        n /= (sizeof(uintptr_t) * 8);
        bitmap = (uintptr_t *)
                             ((uintptr_t) p & ~((uintptr_t) ngx_pagesize - 1));

        /*�ٴ��ж�bitmap��Ӧλ�Ƿ�Ϊ1*/
        if (bitmap[n] & m) {

            /*
             * page->next == NULL������ҳ��ǰ��ȫ��ҳ�������ͷ���һ���ڴ�飬�˻�Ϊ����ҳ��
             * ��Ҫ�����Ӧ�İ���ҳ������
             */
            if (page->next == NULL) {
                /*��ȡ��ҳ��Ӧ�İ���ҳ����*/
                slots = (ngx_slab_page_t *)
                                   ((u_char *) pool + sizeof(ngx_slab_pool_t));
                slot = shift - pool->min_shift;

                /*
                 * ����ҳ���뵽��Ӧ�ڴ���С�İ���ҳ������ײ�,
                 * slots[slot].next��ȡԭ����ҳ�����ײ�
                 */
                page->next = slots[slot].next;
                slots[slot].next = page;

                page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_SMALL;
                page->next->prev = (uintptr_t) page | NGX_SLAB_SMALL;
            }

            /*�����ڴ���Ӧ��bitmapλ����*/
            bitmap[n] &= ~m;

            /*
             * ��������������ڴ��ָʾ�ڴ���Ƿ�ʹ�õ�bitmapռ���˼����ڴ�飬����һ����Ϊһ��
             * 1 << (ngx_pagesize_shift - shift)�������һҳ���ڴ�������������8��
             * ��ʾ��Ҫ��sizeof(uintptr_t)�����ֽ������ٳ����ڴ���С��������Ҫ�����ڴ�������������bitmap
             */
            n = (1 << (ngx_pagesize_shift - shift)) / 8 / (1 << shift);

            if (n == 0) {
                n = 1;
            }

            /*
             * �����������Ҫ�����жϸ�ҳ�е������ڴ���Ƿ�û��ʹ�ã����������뵽free������
             * ��Ϊ�����ڷ����ǰn���ڴ�����ڴ��bitmap������((uintptr_t) 1 << n) - 1)�������
             * ���bitmap���ڴ���ڵ�һ��bitmap�еĶ�Ӧλ��1�����
             * bitmap[0] & ~(((uintptr_t) 1 << n) - 1)Ϊ1���������ڴ�黹��ʹ����û���ͷ�
             */
            if (bitmap[0] & ~(((uintptr_t) 1 << n) - 1)) {
                goto done;
            }

            /*��������������ڴ����һҳ����Ҫʹ�ö��ٸ�bitmap���ܹ���ʾʹ�����*/
            map = (1 << (ngx_pagesize_shift - shift)) / (sizeof(uintptr_t) * 8);

            /*�ж�����bitmap�Ƿ����ڴ����ʹ��*/
            for (n = 1; n < map; n++) {
                if (bitmap[n]) {
                    goto done;
                }
            }

            /*
             * �������ִ�е�����������ҳ�����п����ڷ�����ڴ�鶼û��ʹ��(�����ڴ��bitmap���Ǹ��ڴ��)��
             * ����Ҫ����ҳ���뵽free����(��������)
             */
            ngx_slab_free_pages(pool, page, 1);

            goto done;
        }

        goto chunk_already_free;

    case NGX_SLAB_EXACT:

        /*
         * ����NGX_SLAB_EXACT��p & (ngx_pagesize - 1)��ȡ���Ǵ��ͷ��ڴ�����������ҳ��ƫ����,
         * ������ngx_slab_exact_shift���������Ǵ˿��ڴ�������ҳ�еĵڼ����ڴ�飬Ҳ����bitmap
         * �е�λ��
         */
        m = (uintptr_t) 1 <<
                (((uintptr_t) p & (ngx_pagesize - 1)) >> ngx_slab_exact_shift);
        size = ngx_slab_exact_size;

        /*�ڴ���׵�ַ���ڸ��ڴ���С�����ǵ�ַ�����*/
        if ((uintptr_t) p & (size - 1)) {
            goto wrong_chunk;
        }

        /*������ڿ���ʹ��*/
        if (slab & m) {
            /*slab == NGX_SLAB_BUSY����֮ǰ��ҳ��ȫ��ҳ�������ͷ�������һ�飬˵����Ҫ�������ҳ������*/
            if (slab == NGX_SLAB_BUSY) {
                slots = (ngx_slab_page_t *)
                                   ((u_char *) pool + sizeof(ngx_slab_pool_t));
                slot = ngx_slab_exact_shift - pool->min_shift;

                page->next = slots[slot].next;
                slots[slot].next = page;

                page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_EXACT;
                page->next->prev = (uintptr_t) page | NGX_SLAB_EXACT;
            }

            /*��Ӧbitmapλ����*/
            page->slab &= ~m;

            /*��ҳ�л��������ڴ���ѷ���*/
            if (page->slab) {
                goto done;
            }

            /*����ִ�е����������ҳ�Ѿ��˻�Ϊ����ҳ����Ҫ���뵽free������*/
            ngx_slab_free_pages(pool, page, 1);

            goto done;
        }

        goto chunk_already_free;

    case NGX_SLAB_BIG:

        /*����NGX_SLAB_BIG���͵��ڴ�飬��ҳ����ṹ�е�slab�ĵ���λ���ڱ�ʾ��Ӧ�ڴ���С��λ��*/
        shift = slab & NGX_SLAB_SHIFT_MASK;
        size = 1 << shift;

        /*���ͷ��ڴ��׵�ַ���ڸ��ڴ���С��˵�Ƕ����*/
        if ((uintptr_t) p & (size - 1)) {
            goto wrong_chunk;
        }

        /*
         * ����֪��������NGX_SLAB_BIG���͵�ҳ����slab��Ա�ĸ�16λ���ڱ�ʾbitmap(��Щ����16λ�ģ�
         * �ʹӵ͵���ѡ����Ӧλ��Ϊbitmap),��һ���������m���Ǵ��ͷ��ڴ�����bitmap�е���Ӧλ
         * �����ֽ�����:
         *   1.p & (ngx_pagesize - 1)������ͷ��ڴ�����������ҳҳ�׵�ƫ����������shift�ø��ڴ����
         *     �ڸ�ҳ���ǵڼ����ڴ�飬����bitmap�ж�Ӧ��λ��ƫ�������ڴ˻����ϼ�NGX_SLAB_MAP_SHIFT��
         *     �õ��ľ����ڸ�16λ�е�ƫ������Ҳ��������bitmap�е�ƫ����
         */
        m = (uintptr_t) 1 << ((((uintptr_t) p & (ngx_pagesize - 1)) >> shift)
                              + NGX_SLAB_MAP_SHIFT);

        /*�����Ӧ�ڴ��Ŀǰȷʵ�ѷ���*/
        if (slab & m) {

            /*page->next == NULL������ҳ֮ǰ��һ��ȫ��ҳ�������ͷ�һ�����Ҫ���뵽����ҳ��*/
            if (page->next == NULL) {
                slots = (ngx_slab_page_t *)
                                   ((u_char *) pool + sizeof(ngx_slab_pool_t));
                slot = shift - pool->min_shift;

                page->next = slots[slot].next;
                slots[slot].next = page;

                page->prev = (uintptr_t) &slots[slot] | NGX_SLAB_BIG;
                page->next->prev = (uintptr_t) page | NGX_SLAB_BIG;
            }

            /*��Ӧbitmapλ����*/
            page->slab &= ~m;

            /*��16λ��ʾ��bitmap�������ڴ���ѷ����ȥ�����ǿ���ҳ*/
            if (page->slab & NGX_SLAB_MAP_MASK) {
                goto done;
            }

            /*����ִ�е����������ҳ�Ѿ��ǿ���ҳ�ˣ���Ҫ���뵽free������*/
            ngx_slab_free_pages(pool, page, 1);

            goto done;
        }

        goto chunk_already_free;

    case NGX_SLAB_PAGE:
        
        /*�жϵ�ַ���룬ҳ��ַ��ҳ��СҲ�Ƕ����*/
        if ((uintptr_t) p & (ngx_pagesize - 1)) {
            goto wrong_chunk;
        }

        /*slab==NGX_SLAB_PAGE_FREE�������ͷŵ�ҳ��ʵ�Ѿ��ͷŹ���*/
        if (slab == NGX_SLAB_PAGE_FREE) {
            ngx_slab_error(pool, NGX_LOG_ALERT,
                           "ngx_slab_free(): page is already free");
            goto fail;
        }

        /*
         * slab == NGX_SLAB_PAGE_BUSY,�������ͷŵ�ҳ�����Ƕ������ҳ����ҳ������ֱ���ͷţ�
         * �����⼸��pageһ���ͷţ����pָ��ָ���������page������ʧ��
         */
        if (slab == NGX_SLAB_PAGE_BUSY) {
            ngx_slab_error(pool, NGX_LOG_ALERT,
                           "ngx_slab_free(): pointer to wrong page");
            goto fail;
        }

        /*������n������Ǹô��ͷŵ�ҳ��������ҳ��ƫ����������pages�����е��±�*/
        n = ((u_char *) p - pool->start) >> ngx_pagesize_shift;
        size = slab & ~NGX_SLAB_PAGE_START;  //�������������ҳ��˵����slab��ʾ���Ǻ�������������ҳ�������������Լ�

        ngx_slab_free_pages(pool, &pool->pages[n], size);

        ngx_slab_junk(p, size << ngx_pagesize_shift);

        return;
    }

    /* not reached */

    return;

done:

    ngx_slab_junk(p, size);

    return;

wrong_chunk:

    ngx_slab_error(pool, NGX_LOG_ALERT,
                   "ngx_slab_free(): pointer to wrong chunk");

    goto fail;

chunk_already_free:

    ngx_slab_error(pool, NGX_LOG_ALERT,
                   "ngx_slab_free(): chunk is already free");

fail:

    return;
}


static ngx_slab_page_t *
ngx_slab_alloc_pages(ngx_slab_pool_t *pool, ngx_uint_t pages)
{
    ngx_slab_page_t  *page, *p;

    /*����һҳ�ڴ棬��Ҫ�ӿ���ҳ��������������ǰ���ҳ����*/
    /*���page == &pool->free��˵���Ѿ�û�п���ҳ�����ڷ�����*/
    for (page = pool->free.next; page != &pool->free; page = page->next) {

        /*page->slab�����������������õ�ҳ�����������������������ӵ�*/
        if (page->slab >= pages) {

            /*�������õ�ҳ���������ڴ˴������ҳ������*/
            if (page->slab > pages) {

                /*��ʣ���������õ�ҳ�����һ��ҳ��prevָ��ָ��ʣ�����ҳ��ҳ��ַ*/
                page[page->slab - 1].prev = (uintptr_t) &page[pages];

                /*
                 * 1.����ʣ���������õ�ҳ������
                 * 2.��ʣ����õ�ҳ��ɵĿ���뵽free������
                 * 3.�������õ�ҳ֮�䲢����ͨ����������һ���
                 */
                page[pages].slab = page->slab - pages;
                page[pages].next = page->next;
                page[pages].prev = page->prev;

                /*��ʣ���������ҳ���뵽free������*/
                p = (ngx_slab_page_t *) page->prev;
                p->next = &page[pages];
                page->next->prev = (uintptr_t) &page[pages];

            } else {   /*��������ҳ�������պÿ������ڴ˴η��䣬��������free����*/
                p = (ngx_slab_page_t *) page->prev;
                p->next = page->next;
                page->next->prev = page->prev;
            }

            /*
             * 1.�����������ҳ����ҳ��slab������Ҫ�洢ҳ�������⣬����Ҫ������������������ҳ����ҳ
             * 2.pageҳ�治�����ڴ��(chunk)ʱ��,��������ҳ�������û�,pre�ĺ���λΪNGX_SLAB_PAGE
             * 3.�����������ҳ֮�䲻��ͨ����������һ��ģ������Ҫnext��prevΪNULL��������prevָ�뻹��
             *    ����һ�����ã�������������Ҫ������ΪNGX_SLAB_PAGE��������ҳ�������û�
             */
            page->slab = pages | NGX_SLAB_PAGE_START;
            page->next = NULL;
            page->prev = NGX_SLAB_PAGE;

            /*��������ҳ����һ�������ڴ�ֱ�ӷ����������ҳ�ĵ�ַ*/
            if (--pages == 0) {
                return page;
            }

            /*
             * 1.��������ҳ����������һ�����������ҳ֮�⣬�����ҳ��slab��Ҫ��ΪNGX_SLAB_PAGE_BUSY,
             *   ������������������ҳ�ĺ���ҳ
             * 2.�����������ҳ֮�䲻��ͨ����������һ��ģ������Ҫnext��prevΪNULL��������prevָ�뻹��
                 ����һ�����ã�������������Ҫ������ΪNGX_SLAB_PAGE��������ҳ�������û�
             */
            for (p = page + 1; pages; pages--) {
                p->slab = NGX_SLAB_PAGE_BUSY;
                p->next = NULL;
                p->prev = NGX_SLAB_PAGE;
                p++;
            }

            return page;
        }
    }

    if (pool->log_nomem) {
        ngx_slab_error(pool, NGX_LOG_CRIT,
                       "ngx_slab_alloc() failed: no memory");
    }

    return NULL;
}

/*������������ͷ�ҳ�棬���뵽free������*/
static void
ngx_slab_free_pages(ngx_slab_pool_t *pool, ngx_slab_page_t *page,
    ngx_uint_t pages)
{
    ngx_uint_t        type;
    ngx_slab_page_t  *prev, *join;

    /*������ͷ�ҳ�����ж��ٸ�������ҳ����Ϊpage->slab�����������ڱ�ʾ�������ҳ����ҳ�ı�־�������������¸�ֵ*/ 
    /*����pages--��Ŀ���ǽ������������Ҳ������������������������*/
    page->slab = pages--;

    /*������ͷŵ�ҳ�ǰ����������ҳ��(pages > 1)*/
    if (pages) {
        /*������ҳ������к�������ҳ�е����ݶ����㣬��Ϊ������Щ����ҳ�Ĺ�����Ϣ������ҳ����ṹ�п��Ի�֪*/
        ngx_memzero(&page[1], pages * sizeof(ngx_slab_page_t));
    }

    /*
     *��������ԭ���İ���ҳ������Ϊ���ͷŵ����ҳ���߶��ҳ֮ǰ�������ڴ�ŵ��ڴ�����������¼���:
     *       #define NGX_SLAB_PAGE        0
     *       #define NGX_SLAB_BIG         1
     *       #define NGX_SLAB_EXACT       2
     *       #define NGX_SLAB_SMALL       3
     * ����Ҫ�������ȡprevָ������ָ��ĵ�ַ
     */
    if (page->next) {
        prev = (ngx_slab_page_t *) (page->prev & ~NGX_SLAB_PAGE_MASK);
        prev->next = page->next;
        page->next->prev = page->prev;
    }

    /*joinָ����Ǵ��ͷ�ҳ(����ҳ)����һ��ҳ��ַ*/
    join = page + page->slab;

    /*������ҳ����slab�ڴ��пɷ��������ҳ�У�������ָ������һ��ҳ*/
    if (join < pool->last) {
        //��ȡ��ҳ���ڴ������
        type = join->prev & NGX_SLAB_PAGE_MASK;

        //�������ΪNGX_SLAB_PAGE,������ҳ��δ���ڷ����Ӧ�ھ����С���ڴ�飬����ҳδʹ��
        if (type == NGX_SLAB_PAGE) {

            //join->next != NULL������ҳ�Ѿ��ڿ��������У�������뵽���ͷ�ҳ(Ⱥ)�У�����и�������ҳ�Ĵ�ҳȺ
            if (join->next != NULL) {
                pages += join->slab;
                page->slab += join->slab;

                prev = (ngx_slab_page_t *) (join->prev & ~NGX_SLAB_PAGE_MASK);
                prev->next = join->next;
                join->next->prev = join->prev;

                join->slab = NGX_SLAB_PAGE_FREE;
                join->next = NULL;
                join->prev = NGX_SLAB_PAGE;
            }
        }
    }

    /*�����ҳ��������ҳ���׸�ҳ��������pages�������Ԫ��*/
    if (page > pool->pages) {
        /*��ȡ��ҳ��ǰһ��ҳ��������жϿ��Ա�֤join����Խ��*/
        join = page - 1;
        type = join->prev & NGX_SLAB_PAGE_MASK;  //��ȡ��ҳ���ڴ������

        //�������ΪNGX_SLAB_PAGE,������ҳ��δ���ڷ����Ӧ�ھ����С���ڴ�飬����ҳδʹ��
        if (type == NGX_SLAB_PAGE) {

            /*slab==NGX_SLAB_PAGE_FREE�������ͷŵ�ҳ��ʵ�Ѿ��ͷŹ���*/
            if (join->slab == NGX_SLAB_PAGE_FREE) {
                join = (ngx_slab_page_t *) (join->prev & ~NGX_SLAB_PAGE_MASK);
            }

            //join->next != NULL������ҳ�Ѿ��ڿ��������У�������뵽���ͷ�ҳ(Ⱥ)�У�����и�������ҳ�Ĵ�ҳȺ
            if (join->next != NULL) {
                pages += join->slab;
                join->slab += page->slab;

                prev = (ngx_slab_page_t *) (join->prev & ~NGX_SLAB_PAGE_MASK);
                prev->next = join->next;
                join->next->prev = join->prev;

                page->slab = NGX_SLAB_PAGE_FREE;
                page->next = NULL;
                page->prev = NGX_SLAB_PAGE;

                page = join;
            }
        }
    }

    
    if (pages) {
        //page[pages]��������ҳ�����һ��ҳ����prevָ������ҳ����ҳ
        page[pages].prev = (uintptr_t) page;
    }

    /*���ͷŵ�ҳ(Ⱥ)���뵽���뵽����������*/
    page->prev = (uintptr_t) &pool->free;
    page->next = pool->free.next;

    page->next->prev = (uintptr_t) page;

    pool->free.next = page;
}


static void
ngx_slab_error(ngx_slab_pool_t *pool, ngx_uint_t level, char *text)
{
    ngx_log_error(level, ngx_cycle->log, 0, "%s%s", text, pool->log_ctx);
}

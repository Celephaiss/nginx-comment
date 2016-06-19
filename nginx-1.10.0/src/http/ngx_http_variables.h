
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_HTTP_VARIABLES_H_INCLUDED_
#define _NGX_HTTP_VARIABLES_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>


typedef ngx_variable_value_t  ngx_http_variable_value_t;

#define ngx_http_variable(v)     { sizeof(v) - 1, 1, 0, 0, 0, (u_char *) v }

typedef struct ngx_http_variable_s  ngx_http_variable_t;

typedef void (*ngx_http_set_variable_pt) (ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);

/*
 * *************************************************������������ָ��*********************************************
 *     ����r��data����������������ָ�룬v����������ű���ֵ��(�ڴ��ڵ��øú����ĵط��Ѿ��������),��Ȼ�������õ�
 * �ڴ��ǲ�������ű���ֵ���ڴ�ġ�����ű���ֵ���ڴ������������r�е��ڴ�ؽ��з��䣬�����������ʱ�������ڴ�
 * �ͻᱻ�ͷţ�����ֵ���������ں�������һ�µ�,����������Ȼ��
 * 
 *     uintptr_t data��ͨ���淨:
 * 1. �ò����������á����ֻ������һЩ�������޹صı���ֵ������Բ��øò���ֵ
 * 2. �ò�����ָ��ʹ�á������ڽ����������������ʱ��ʹ�ã����������������ַ�����ͷ����Щ�����Ľ���������ͬС�죬
 *    ����ͨ����������������r->headers_in.headers���飬�ҵ���Ӧ�ı������󷵻���ֵ���ɡ���ʱdata���������������ַ�á�
 * 3. �ò���������ʹ�á������ڱ���ṹ���г�Ա��ƫ��������Щʱ�򣬱���ֵ�ܿ��ܾ���ԭʼhttp�ַ����е�һ���������ַ�����
 *    �����ֱ�Ӹ��ã��Ͳ�����Ϊ����ֵ�����ڴ�;���⣬http��ܺ��п���������Ľ����������Ѿ��õ�����Ӧ�ı���ֵ����ʱ
 *    Ҳ���Ը��á���http_host�����Ѿ��ڽ�������ͷ����ʱ��������ˡ����õ�������:http��ܽ�������ı���ֵ���䶨���Ա
 *    ��ngx_http_request_t�ṹ�����λ���ǹ̶�����ġ������Ϳ�����data������ƫ��������ngx_http_variable_value_t��
 *    data��len��Աָ�����ֵ�ַ������ɡ�
 */
typedef ngx_int_t (*ngx_http_get_variable_pt) (ngx_http_request_t *r,
    ngx_http_variable_value_t *v, uintptr_t data);


/*���������ԣ�ֵ�ɱ䡢�����桢����������hash*/
#define NGX_HTTP_VAR_CHANGEABLE   1
#define NGX_HTTP_VAR_NOCACHEABLE  2
#define NGX_HTTP_VAR_INDEXED      4
#define NGX_HTTP_VAR_NOHASH       8

/*     һ����������ͬʱ�ȱ������ֱ�hash����һ��ֻ��һ��������������������һ����������ͬʱӵ������ngx_http_variable_t
 * �ṹ�壬��ʱ�Ĳ�����������ṹ���flags��Ա��ͬ��
 *
 *     �洢����ֵ�Ľṹ��ngx_http_variable_value_t�����ڶ�ȡ����ֵ��ʱ�򱻴�����Ҳ�п����ڳ�ʼ��һ��http�����
 * ʱ�򱻴�����ngx_http_request_t�ṹ������У�����������ַ�ʽ��ngx_http_variable_t�ṹ���Ա�ĸ�ֵ�������
 */

/*��������ṹ��*/
struct ngx_http_variable_s {
    ngx_str_t                     name;   /*����������������ǰ�õ�$����*//* must be first to build the hash */
    ngx_http_set_variable_pt      set_handler;  /*�����Ҫ�����������ֵ��ʱ����б���ֵ���ã���ʵ�ָ÷���*/
    ngx_http_get_variable_pt      get_handler;  /*ÿ�λ�ȡ����ֵʱ����ø÷���*/
    uintptr_t                     data;   /*��Ϊget_handler����set_handler�����Ĳ���*/
    ngx_uint_t                    flags;  /*���������ԣ���ֵ�ɱ䡢�������������桢��hash��*/
    ngx_uint_t                    index;  /*����ֵ������Ļ��������е�����ֵ*/
};


ngx_http_variable_t *ngx_http_add_variable(ngx_conf_t *cf, ngx_str_t *name,
    ngx_uint_t flags);
ngx_int_t ngx_http_get_variable_index(ngx_conf_t *cf, ngx_str_t *name);
ngx_http_variable_value_t *ngx_http_get_indexed_variable(ngx_http_request_t *r,
    ngx_uint_t index);
ngx_http_variable_value_t *ngx_http_get_flushed_variable(ngx_http_request_t *r,
    ngx_uint_t index);

ngx_http_variable_value_t *ngx_http_get_variable(ngx_http_request_t *r,
    ngx_str_t *name, ngx_uint_t key);

ngx_int_t ngx_http_variable_unknown_header(ngx_http_variable_value_t *v,
    ngx_str_t *var, ngx_list_part_t *part, size_t prefix);


#if (NGX_PCRE)

typedef struct {
    ngx_uint_t                    capture;
    ngx_int_t                     index;
} ngx_http_regex_variable_t;


typedef struct {
    ngx_regex_t                  *regex;
    ngx_uint_t                    ncaptures;
    ngx_http_regex_variable_t    *variables;
    ngx_uint_t                    nvariables;
    ngx_str_t                     name;
} ngx_http_regex_t;


typedef struct {
    ngx_http_regex_t             *regex;
    void                         *value;
} ngx_http_map_regex_t;


ngx_http_regex_t *ngx_http_regex_compile(ngx_conf_t *cf,
    ngx_regex_compile_t *rc);
ngx_int_t ngx_http_regex_exec(ngx_http_request_t *r, ngx_http_regex_t *re,
    ngx_str_t *s);

#endif


typedef struct {
    ngx_hash_combined_t           hash;
#if (NGX_PCRE)
    ngx_http_map_regex_t         *regex;
    ngx_uint_t                    nregex;
#endif
} ngx_http_map_t;


void *ngx_http_map_find(ngx_http_request_t *r, ngx_http_map_t *map,
    ngx_str_t *match);


ngx_int_t ngx_http_variables_add_core_vars(ngx_conf_t *cf);
ngx_int_t ngx_http_variables_init_vars(ngx_conf_t *cf);


extern ngx_http_variable_value_t  ngx_http_variable_null_value;
extern ngx_http_variable_value_t  ngx_http_variable_true_value;


#endif /* _NGX_HTTP_VARIABLES_H_INCLUDED_ */

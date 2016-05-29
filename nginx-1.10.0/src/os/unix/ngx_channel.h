
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef _NGX_CHANNEL_H_INCLUDED_
#define _NGX_CHANNEL_H_INCLUDED_


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


typedef struct {
    ngx_uint_t  command;   //���ݵ�tcp��Ϣ�е�����
    ngx_pid_t   pid;       //����ID��һ��������ͷ��Ľ���ID
    ngx_int_t   slot;      //����ͷ���ngx_processes���������е����(�����±�)
    ngx_fd_t    fd;        //ͨ�ŵ��׽��־��
} ngx_channel_t;

/*
 * ��ngx_channel_t�ṹ���е�command��Ա���Ѷ�������������¼���:
 * 1.NGX_CMD_OPEN_CHANNEL   ʹ��Ƶ��ͨ��ǰ���뷢�͵������Ƶ��
 * 2.NGX_CMD_CLOSE_CHANNEL  ʹ����Ƶ��ͨ�ź���뷢�͵�����ر�Ƶ��
 * 3.NGX_CMD_QUIT           Ҫ��������շ��������˳�����
 * 4.NGX_CMD_TERMINATE      Ҫ��������շ�ǿ�����˳�����
 * 5.NGX_CMD_REOPEN         Ҫ��������շ����´򿪽����Ѿ��򿪹����ļ�
 */


ngx_int_t ngx_write_channel(ngx_socket_t s, ngx_channel_t *ch, size_t size,
    ngx_log_t *log);
ngx_int_t ngx_read_channel(ngx_socket_t s, ngx_channel_t *ch, size_t size,
    ngx_log_t *log);
ngx_int_t ngx_add_channel_event(ngx_cycle_t *cycle, ngx_fd_t fd,
    ngx_int_t event, ngx_event_handler_pt handler);
void ngx_close_channel(ngx_fd_t *fd, ngx_log_t *log);


#endif /* _NGX_CHANNEL_H_INCLUDED_ */

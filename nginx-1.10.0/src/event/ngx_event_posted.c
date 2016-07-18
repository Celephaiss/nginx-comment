
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>


/*
 *     Nginx���ڳ�ַ��Ӷ��cpu�ܹ����ܵĿ��ǣ�ʹ���˶��worker�ӽ��̼�����ͬ�˿ڵ���ƣ��������worker�ӽ�����accept
 * ����������ʱ������������������������"��Ⱥ"���⡣�����ڽ�������ʱ�����漰�����ؾ�������⣬�ڶ��worker�ӽ���
 * ��������һ���������¼�ʱ��һ��ֻ��һ��worker�ӽ������ջ�ɹ��������ӣ��������һֱ�����������ֱ�����ӹرա�
 * ����������������Ľ���벻��Nginx��post���ơ����post���Ʊ�ʾ���������¼��Ӻ�ִ�С�Nginx���������post���У�һ��
 * ���ɱ������ļ������ӵĶ��¼����ɵ�ngx_posted_accept_events���У�һ��������ͨ��/д�¼����ɵ�ngx_posted_events����
 * post���Ƶľ��幦������:
 *     1.��epoll_wait������һ���¼����ֵ������������У��ô�����������¼���ngx_posted_accept_events��������ִ�У������
 * ��ͨ�¼���ngx_posted_events���к���ִ�С����ǽ�����ؾ����"��Ⱥ"�Ĺؼ���
 *     2.����ڴ���һ���¼��Ĺ����в�������һ���¼���������ϣ������¼����ִ��(��������ִ��)���Ϳ��Խ�����뵽post
 * �����С�
 */


ngx_queue_t  ngx_posted_accept_events;  //����ű������ļ������ӵĶ��¼�
ngx_queue_t  ngx_posted_events;  //�������ͨ�Ķ�д�¼�

/*��������post�¼�����*/
void
ngx_event_process_posted(ngx_cycle_t *cycle, ngx_queue_t *posted)
{
    ngx_queue_t  *q;
    ngx_event_t  *ev;

    while (!ngx_queue_empty(posted)) {

        q = ngx_queue_head(posted);
        ev = ngx_queue_data(q, ngx_event_t, queue);

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                      "posted event %p", ev);

        ngx_delete_posted_event(ev);  //�Ӷ������Ƴ�����¼����ڴ沢û���ͷţ�ֻ�Ǵ�˫������������

        ev->handler(ev);  //������������¼�����ô���handler����ngx_event_accept()��������
    }
}

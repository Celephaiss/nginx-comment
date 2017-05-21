
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>


/*
 * The red-black tree code is based on the algorithm described in
 * the "Introduction to Algorithms" by Cormen, Leiserson and Rivest.
 */


static ngx_inline void ngx_rbtree_left_rotate(ngx_rbtree_node_t **root,
    ngx_rbtree_node_t *sentinel, ngx_rbtree_node_t *node);
static ngx_inline void ngx_rbtree_right_rotate(ngx_rbtree_node_t **root,
    ngx_rbtree_node_t *sentinel, ngx_rbtree_node_t *node);


void
ngx_rbtree_insert(ngx_rbtree_t *tree, ngx_rbtree_node_t *node)
{
    ngx_rbtree_node_t  **root, *temp, *sentinel;

    /* a binary tree insert */

    /* ��ȡ������ĸ��ڵ���ڱ��ڵ� */
    root = (ngx_rbtree_node_t **) &tree->root;
    sentinel = tree->sentinel;

    /* 
     * ������ڵ�����ڱ��ڵ㣬˵����ʱ�������һ�ſ�������ô�ͽ�������Ľڵ�
     * ��Ϊ���ڵ㣬Ⱦ�ɺ�ɫ�������������ӽڵ�����Ϊ�ڱ��ڵ㣬���ڵ�û�и��ڵ�
     */
    if (*root == sentinel) {
        node->parent = NULL;
        node->left = sentinel;
        node->right = sentinel;
        ngx_rbt_black(node);
        *root = node;

        return;
    }

    /* 
     * ����ע���insert�ص�����ֵ���뵽������У���ʱ���������������һ����ͨ��
     * ���������
     */
    tree->insert(*root, node, sentinel);

    /* re-balance tree */
    /* ���������ƽ�� */

    /* ��ǰ�ڵ㲻�Ǹ��ڵ㣬�����丸�ڵ��Ǻ�ɫ�� */
    while (node != *root && ngx_rbt_is_red(node->parent)) {

        /* 1. ��ǰ�ڵ�(��ɫ)�ĸ��ڵ������游�ڵ�����ӽڵ� */
        if (node->parent == node->parent->parent->left) {
            
            /* ��ȡ��ǰ�ڵ������ڵ�(�游�ڵ�����ӽڵ�) */
            temp = node->parent->parent->right;

            /*
             * �����ǰ�ڵ������ڵ��Ǻ�ɫ�ģ���ô�����ڵ������ڵ㶼Ⱦ�ɺ�ɫ��
             * ���游�ڵ�Ⱦ�ɺ�ɫ��������Ϊ��ǰ�ڵ㣬��һ�ֻ����������ƴ���
             */
            if (ngx_rbt_is_red(temp)) {
                ngx_rbt_black(node->parent);
                ngx_rbt_black(temp);
                ngx_rbt_red(node->parent->parent);
                node = node->parent->parent;

            } else {
                /* ��������������˵��������ڵ��Ǻ�ɫ�� */
                
                /* 
                 * �����ǰ�ڵ����丸�ڵ�����ӽڵ㣬��ô���丸�ڵ�����Ϊ��ǰ�ڵ㣬
                 * ���Ե�ǰ�ڵ��������������ִ����֮��"ԭ��ǰ�ڵ�"�ͱ���˸��ڵ㣬
                 * ��"ԭ���ڵ�"�ͱ����"ԭ��ǰ�ڵ�"���ӽڵ�
                 */
                if (node == node->parent->right) {
                    node = node->parent;
                    ngx_rbtree_left_rotate(root, sentinel, node);
                }

                /* ����ǰ�ڵ�ĸ��ڵ�Ⱦ�ɺ�ɫ���������ڵ������ڵ��ֶ��Ǻ�ɫ���� */
                ngx_rbt_black(node->parent);

                /* ���游�ڵ�����Ϊ��ɫ�������游�ڵ������������ */
                ngx_rbt_red(node->parent->parent);
                ngx_rbtree_right_rotate(root, sentinel, node->parent->parent);
            }

        } else {
            /*
             * ����ִ�������뵽����˵����ǰ�ڵ�ĸ��ڵ������游�ڵ�����ӽڵ�
             */

            /* ��ȡ��ǰ�ڵ������ڵ� */
            temp = node->parent->parent->left;

            /* 
             * �����ǰ�ڵ������ڵ��Ǻ�ɫ�ģ���ô�ͽ��丸�ڵ������ڵ㶼
             * Ⱦ�ɺ�ɫ�������游�ڵ�Ⱦ�ɺ�ɫ��������Ϊ��ǰ�ڵ㣬��һ��ѭ��
             * ������������ƵĴ���
             */
            if (ngx_rbt_is_red(temp)) {
                ngx_rbt_black(node->parent);
                ngx_rbt_black(temp);
                ngx_rbt_red(node->parent->parent);
                node = node->parent->parent;

            } else {
                /* ����ִ�������뵽����˵����ǰ�ڵ������ڵ��Ǻ�ɫ�� */

                /* 
                 * �����ǰ�ڵ����丸�ڵ�����ӽڵ㣬��ô���丸�ڵ�����Ϊ��ǰ�ڵ㣬
                 * �����������������
                 */
                if (node == node->parent->left) {
                    node = node->parent;
                    ngx_rbtree_right_rotate(root, sentinel, node);
                }

                /* ����ǰ�ڵ�ĸ��ڵ�Ⱦ�ɺ�ɫ���������ڵ������ڵ�;�Ϊ��ɫ�� */
                ngx_rbt_black(node->parent);

                /* ����ǰ�ڵ���游�ڵ�Ⱦ�ɺ�ɫ������������������� */
                ngx_rbt_red(node->parent->parent);
                ngx_rbtree_left_rotate(root, sentinel, node->parent->parent);
            }
        }
    }

    /* �����ڵ�Ⱦ�ɺ�ɫ */
    ngx_rbt_black(*root);
}

/* insert�ص���ʵ����ͨ����������Ĳ������ */
void
ngx_rbtree_insert_value(ngx_rbtree_node_t *temp, ngx_rbtree_node_t *node,
    ngx_rbtree_node_t *sentinel)
{
    ngx_rbtree_node_t  **p;

    for ( ;; ) {

        p = (node->key < temp->key) ? &temp->left : &temp->right;

        if (*p == sentinel) {
            break;
        }

        temp = *p;
    }

    /* ÿһ���²���Ľڵ㣬������Ϊ������Ҷ�ӽڵ�(���ڱ��ڵ�)�����ᱻȾ�ɺ�ɫ */
    *p = node;
    node->parent = temp;
    node->left = sentinel;
    node->right = sentinel;
    ngx_rbt_red(node);
}


/* 
 * ngx_rbtree_insert_timer_value()����˼��ú�������������ʱ���¼�ʱʹ�õ�insert
 * �ص���������ʱ��node�ڵ���ĳ���¼�����ĳ�Ա��������Բο�ngx_event_t�ṹ��
 */
void
ngx_rbtree_insert_timer_value(ngx_rbtree_node_t *temp, ngx_rbtree_node_t *node,
    ngx_rbtree_node_t *sentinel)
{
    ngx_rbtree_node_t  **p;

    for ( ;; ) {

        /*
         * Timer values
         * 1) are spread in small range, usually several minutes,
         * 2) and overflow each 49 days, if milliseconds are stored in 32 bits.
         * The comparison takes into account that overflow.
         */

        /*  node->key < temp->key */

        p = ((ngx_rbtree_key_int_t) (node->key - temp->key) < 0)
            ? &temp->left : &temp->right;

        if (*p == sentinel) {
            break;
        }

        temp = *p;
    }

    /* ÿһ���²���Ľڵ㣬������Ϊ������Ҷ�ӽڵ�(���ڱ��ڵ�)�����ᱻȾ�ɺ�ɫ */
    *p = node;
    node->parent = temp;
    node->left = sentinel;
    node->right = sentinel;
    ngx_rbt_red(node);
}


void
ngx_rbtree_delete(ngx_rbtree_t *tree, ngx_rbtree_node_t *node)
{
    ngx_uint_t           red;
    ngx_rbtree_node_t  **root, *sentinel, *subst, *temp, *w;

    /* a binary tree delete */

    root = (ngx_rbtree_node_t **) &tree->root;
    sentinel = tree->sentinel;

    if (node->left == sentinel) {
        temp = node->right;
        subst = node;

    } else if (node->right == sentinel) {
        temp = node->left;
        subst = node;

    } else {
        subst = ngx_rbtree_min(node->right, sentinel);

        if (subst->left != sentinel) {
            temp = subst->left;
        } else {
            temp = subst->right;
        }
    }

    if (subst == *root) {
        *root = temp;
        ngx_rbt_black(temp);

        /* DEBUG stuff */
        node->left = NULL;
        node->right = NULL;
        node->parent = NULL;
        node->key = 0;

        return;
    }

    red = ngx_rbt_is_red(subst);

    if (subst == subst->parent->left) {
        subst->parent->left = temp;

    } else {
        subst->parent->right = temp;
    }

    if (subst == node) {

        temp->parent = subst->parent;

    } else {

        if (subst->parent == node) {
            temp->parent = subst;

        } else {
            temp->parent = subst->parent;
        }

        subst->left = node->left;
        subst->right = node->right;
        subst->parent = node->parent;
        ngx_rbt_copy_color(subst, node);

        if (node == *root) {
            *root = subst;

        } else {
            if (node == node->parent->left) {
                node->parent->left = subst;
            } else {
                node->parent->right = subst;
            }
        }

        if (subst->left != sentinel) {
            subst->left->parent = subst;
        }

        if (subst->right != sentinel) {
            subst->right->parent = subst;
        }
    }

    /* DEBUG stuff */
    node->left = NULL;
    node->right = NULL;
    node->parent = NULL;
    node->key = 0;

    if (red) {
        return;
    }

    /* a delete fixup */

    while (temp != *root && ngx_rbt_is_black(temp)) {

        if (temp == temp->parent->left) {
            w = temp->parent->right;

            if (ngx_rbt_is_red(w)) {
                ngx_rbt_black(w);
                ngx_rbt_red(temp->parent);
                ngx_rbtree_left_rotate(root, sentinel, temp->parent);
                w = temp->parent->right;
            }

            if (ngx_rbt_is_black(w->left) && ngx_rbt_is_black(w->right)) {
                ngx_rbt_red(w);
                temp = temp->parent;

            } else {
                if (ngx_rbt_is_black(w->right)) {
                    ngx_rbt_black(w->left);
                    ngx_rbt_red(w);
                    ngx_rbtree_right_rotate(root, sentinel, w);
                    w = temp->parent->right;
                }

                ngx_rbt_copy_color(w, temp->parent);
                ngx_rbt_black(temp->parent);
                ngx_rbt_black(w->right);
                ngx_rbtree_left_rotate(root, sentinel, temp->parent);
                temp = *root;
            }

        } else {
            w = temp->parent->left;

            if (ngx_rbt_is_red(w)) {
                ngx_rbt_black(w);
                ngx_rbt_red(temp->parent);
                ngx_rbtree_right_rotate(root, sentinel, temp->parent);
                w = temp->parent->left;
            }

            if (ngx_rbt_is_black(w->left) && ngx_rbt_is_black(w->right)) {
                ngx_rbt_red(w);
                temp = temp->parent;

            } else {
                if (ngx_rbt_is_black(w->left)) {
                    ngx_rbt_black(w->right);
                    ngx_rbt_red(w);
                    ngx_rbtree_left_rotate(root, sentinel, w);
                    w = temp->parent->left;
                }

                ngx_rbt_copy_color(w, temp->parent);
                ngx_rbt_black(temp->parent);
                ngx_rbt_black(w->left);
                ngx_rbtree_right_rotate(root, sentinel, temp->parent);
                temp = *root;
            }
        }
    }

    ngx_rbt_black(temp);
}

/*
 * ��������������node�ڵ㼴Ϊ��Ҫ���������Ľڵ㡣
 * ����ͼΪ��:
 *        x                      z
 *       / \         -->        /
 *      y   z                  x
 *                            /
 *                           y
 * ��x����������������ζ����Ҫ��"x�����ӽڵ�"����Ϊ"x�ĸ��ڵ�"����x��������
 * �����ӽڵ�����ӽڵ㣬��������е�"��"��ζ�Ŵ������ڵ���������ӽڵ��
 * ���ӽڵ�
 */
static ngx_inline void
ngx_rbtree_left_rotate(ngx_rbtree_node_t **root, ngx_rbtree_node_t *sentinel,
    ngx_rbtree_node_t *node)
{
    ngx_rbtree_node_t  *temp;

    /* ��ȡ�������ڵ�����ӽڵ� */
    temp = node->right;

    /* �����ӽڵ�����ӽڵ�(�������ڵ�����ӽڵ�)���ø��������ڵ���Ϊ�����ӽڵ� */
    node->right = temp->left;

    /* 
     * ������ӽڵ�����ӽڵ�(�������ڵ�����ӽڵ�)�������ڱ��ڵ㣬
     * ����������ݵĽڵ㣬��ô��Ҫ���ýڵ�ĸ��ڵ�����Ϊ�������ڵ㣬
     * ��Ϊ����ĸ�ֵ�������ýڵ����ø��˴������ڵ���Ϊ�����ӽڵ�
     */
    if (temp->left != sentinel) {
        temp->left->parent = node;
    }

    /* 
     * ��Ϊ�������ڵ�����ӽڵ�Ҫȡ���Լ���λ�ã����Խ��Լ��ĸ��ڵ����ø�
     * ���ӽڵ���Ϊ�丸�ڵ�
     */
    temp->parent = node->parent;

    /*
     * 1. ����������Ľڵ���ڸ��ڵ㣬��ô�ͰѴ������ڵ�����ӽڵ�����Ϊ���ڵ�
     * 2. ����������Ľڵ����丸�ڵ�����ӽڵ㣬��ô�ͰѴ������ڵ�����ӽڵ�
     *    ���ø��丸�ڵ���Ϊ�����ӽڵ㡣
     * 3. ����������Ľڵ����丸�ڵ�����ӽڵ㣬��ô�ͰѴ������ڵ�����ӽڵ�
     *    ���ø��丸�ڵ���Ϊ�����ӽڵ�
     */
    if (node == *root) {
        *root = temp;

    } else if (node == node->parent->left) {
        node->parent->left = temp;

    } else {
        node->parent->right = temp;
    }

    /* 
     * ������Ҫ�������������Ľڵ㣬��Ҫ���ýڵ����ø������ӽڵ���Ϊ�����ӽڵ㣬
     * �����ӽڵ�Ҳ�ͱ���˸ýڵ�ĸ��ڵ� 
     */
    temp->left = node;
    node->parent = temp;
}

/* 
 * ��������������node�ڵ㼴Ϊ��Ҫ���������Ľڵ�
 * ����ͼΪ��:
 *         x                        y
 *        / \          -->           \
 *       y   z                        x
 *                                     \
 *                                      z
 * ��x����������������ζ����Ҫ��"x�����ӽڵ�"����Ϊ"x�ĸ��ڵ�"��������Ҳ�ͱ����
 * �����ӽڵ�����ӽڵ㣬��������е�"��"��ζ�Ŵ������ڵ���������ӽڵ��
 * ���ӽڵ�
 */
static ngx_inline void
ngx_rbtree_right_rotate(ngx_rbtree_node_t **root, ngx_rbtree_node_t *sentinel,
    ngx_rbtree_node_t *node)
{
    ngx_rbtree_node_t  *temp;

    /* ��ȡ�������ڵ�����ӽڵ� */
    temp = node->left;

    /* �����ӽڵ�����ӽڵ�(�������ڵ�����ӽڵ�)���ø��������ڵ���Ϊ�����ӽڵ� */
    node->left = temp->right;

    /* 
     * ������ӽڵ�����ӽڵ�(�������ڵ�����ӽڵ�)�������ڱ��ڵ㣬
     * ����������ݵĽڵ㣬��ô��Ҫ���ýڵ�ĸ��ڵ�����Ϊ�������ڵ㣬
     * ��Ϊ����ĸ�ֵ�������ýڵ����ø��˴������ڵ���Ϊ�����ӽڵ�
     */
    if (temp->right != sentinel) {
        temp->right->parent = node;
    }

    /* 
     * ��Ϊ�������ڵ�����ӽڵ�Ҫȡ���Լ���λ�ã����Խ��Լ��ĸ��ڵ����ø�����
     * �ڵ���Ϊ�丸�ڵ�
     */
    temp->parent = node->parent;

    /*
     * 1. ����������ڵ��Ǹ��ڵ㣬��ô���������ڵ�����ӽڵ�����Ϊ���ڵ�
     * 2. ����������ڵ����丸�ڵ�����ӽڵ㣬��ô�ͰѴ������ڵ�����ӽڵ����ø�
     *    �丸�ڵ���Ϊ�����ӽڵ�
     * 3. ����������ڵ����丸�ڵ�����ӽڵ㣬��ô�ͰѴ������ڵ�����ӽڵ����ø�
     *    �丸�ڵ���Ϊ�����ӽڵ�
     */
    if (node == *root) {
        *root = temp;

    } else if (node == node->parent->right) {
        node->parent->right = temp;

    } else {
        node->parent->left = temp;
    }

    /* 
     * ������Ҫ���������Ľڵ㣬��Ҫ�������ø������ӽڵ���Ϊ���ӽڵ㣬
     * ���������ӽڵ�Ҳ�ͱ���˴������ڵ�ĸ��ڵ�
     */
    temp->right = node;
    node->parent = temp;
}

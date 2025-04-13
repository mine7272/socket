/*****************************************************************************
* File       : server_ws_record.c
* Description: WebSocket ���� - \n ���� ���ڵ� ���� + �α� + ���� �ð� ���� (select ��� ��Ƽ�÷���)
*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/select.h>
#include <unistd.h>
#include <libwebsockets.h>

#define BUF_SIZE 4096
#define MAX_CLIENTS 30

/*****************************************************************************
* Structure  : per_session_data
* Description: ���Ǻ� ����� ������ �� ���� ���
*****************************************************************************/
struct per_session_data
{
    char buffer[BUF_SIZE];
    size_t buffer_len;
    size_t total_bytes;
    int record_count;
    struct timeval start_time;
    int in_use;                   // ���� ��� �� ����
    int fd;                       // ���� ���� ��ũ����
};

/*****************************************************************************
* Structure  : ws_context
* Description: WebSocket ���� ���ؽ�Ʈ ������
*****************************************************************************/
struct ws_context
{
    struct per_session_data sessions[MAX_CLIENTS];
    fd_set read_set;
    int max_fd;
    int server_fd;
    struct lws_context *lws_context; // libwebsockets ���ؽ�Ʈ ���� ����
};

/*****************************************************************************
* Function   : find_free_session
* Description: ��� ������ ���� �ε��� ã��
*****************************************************************************/
static int find_free_session(struct ws_context *context)
{
    int i;
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (!context->sessions[i].in_use)
        {
            return i;
        }
    }
    return -1;
}

/*****************************************************************************
* Function   : handle_established
* Description: �� Ŭ���̾�Ʈ ���� ó��
*****************************************************************************/
static void handle_established(struct ws_context *context, int client_fd)
{
    int session_idx = find_free_session(context);
    
    if (session_idx < 0)
    {
        fprintf(stderr, "SERVER: �ִ� Ŭ���̾�Ʈ ���� �� �ʰ�\n");
        close(client_fd);
        return;
    }
    
    struct per_session_data *pss = &context->sessions[session_idx];
    
    printf("SERVER: Ŭ���̾�Ʈ �����\n");
    pss->buffer_len = 0;
    pss->total_bytes = 0;
    pss->record_count = 0;
    pss->in_use = 1;
    pss->fd = client_fd;
    gettimeofday(&pss->start_time, NULL);
    
    // ���� �߰�
    FD_SET(client_fd, &context->read_set);
    if (client_fd > context->max_fd)
    {
        context->max_fd = client_fd;
    }
}

/*****************************************************************************
* Function   : handle_receive
* Description: Ŭ���̾�Ʈ�κ��� ������ ���� ó��
*****************************************************************************/
static int handle_receive(struct per_session_data *pss, char *in, size_t len)
{
    char *start = NULL;
    char *end = NULL;
    size_t remain = 0;
    
    if (pss->buffer_len + len >= BUF_SIZE)
    {
        fprintf(stderr, "SERVER: ���� �ʰ�\n");
        return -1;
    }
    
    memcpy(pss->buffer + pss->buffer_len, in, len);
    pss->buffer_len += len;
    pss->total_bytes += len;
    
    start = pss->buffer;
    while ((end = memchr(start, '\n', pss->buffer + pss->buffer_len - start)))
    {
        pss->record_count++;
        start = end + 1;
    }
    
    if (start < pss->buffer + pss->buffer_len)
    {
        remain = pss->buffer + pss->buffer_len - start;
        memmove(pss->buffer, start, remain);
        pss->buffer_len = remain;
    }
    else
    {
        pss->buffer_len = 0;
    }
    
    return 0;
}

/*****************************************************************************
* Function   : handle_close
* Description: Ŭ���̾�Ʈ ���� ���� ó��
*****************************************************************************/
static void handle_close(struct ws_context *context, struct per_session_data *pss)
{
    struct timeval end_time;
    double elapsed = 0.0;
    
    gettimeofday(&end_time, NULL);
    elapsed = (end_time.tv_sec - pss->start_time.tv_sec) +
              (end_time.tv_usec - pss->start_time.tv_usec) / 1000000.0;
    
    printf("SERVER: ���� �����\n");
    printf("SERVER: �� ���� ����Ʈ: %zu, ���ڵ� ��: %d, �ҿ� �ð�: %.6f ��\n",
            pss->total_bytes, pss->record_count, elapsed);
    
    // ���� ����
    FD_CLR(pss->fd, &context->read_set);
    close(pss->fd);
    
    // ���� ����
    pss->buffer_len = 0;
    pss->total_bytes = 0;
    pss->record_count = 0;
    pss->in_use = 0;
    pss->fd = -1;
}

/*****************************************************************************
* Function   : callback_server
* Description: WebSocket ���� �ݹ� �Լ�
*****************************************************************************/
static int callback_server(struct lws *wsi, enum lws_callback_reasons reason,
                         void *user, void *in, size_t len)
{
    struct ws_context *context = (struct ws_context *)lws_context_user(lws_get_context(wsi));
    struct per_session_data *pss = (struct per_session_data *)user;
    int fd;
    
    switch (reason) {
        case LWS_CALLBACK_ESTABLISHED:
            fd = lws_get_socket_fd(wsi);
            if (fd >= 0) {
                handle_established(context, fd);
            }
            break;
            
        case LWS_CALLBACK_RECEIVE:
            if (pss->in_use) {
                handle_receive(pss, in, len);
            }
            break;
            
        case LWS_CALLBACK_CLOSED:
            if (pss->in_use) {
                handle_close(context, pss);
            }
            break;
            
        default:
            break;
    }
    
    return 0;
}

/*****************************************************************************
* Structure  : protocols
* Description: ����� WebSocket �������� ����
*****************************************************************************/
static struct lws_protocols protocols[] = {
    {
        "file-transfer",
        callback_server,
        sizeof(struct per_session_data),
        BUF_SIZE,
    },
    { NULL, NULL, 0, 0 }
};

/*****************************************************************************
* Function   : accept_new_connection
* Description: �� Ŭ���̾�Ʈ ���� ����
*****************************************************************************/
static void accept_new_connection(struct ws_context *context)
{
    // libwebsockets �̺�Ʈ ó�� ����
    lws_service(context->lws_context, 0);
}

/*****************************************************************************
* Function   : process_client_data
* Description: Ŭ���̾�Ʈ ������ ó��
*****************************************************************************/
static void process_client_data(struct ws_context *context)
{
    fd_set read_fds;
    struct timeval tv;
    int activity;
    
    // select �غ�
    read_fds = context->read_set;
    tv.tv_sec = 0;
    tv.tv_usec = 10000; // 10ms
    
    activity = select(context->max_fd + 1, &read_fds, NULL, NULL, &tv);
    
    if (activity < 0)
    {
        perror("SERVER: select ����");
        return;
    }
    
    // Ȱ���� ������ libwebsockets�� ó�� ��û
    if (activity > 0) {
        accept_new_connection(context);
    } else {
        // Ÿ�Ӿƿ� �ÿ��� libwebsockets ���� ȣ��
        lws_service(context->lws_context, 0);
    }
}

/*****************************************************************************
* Function   : main
* Description: WebSocket ���� ������, ���� ���� ����
* Parameters : - int argc     : ���� ����
*              - char **argv  : ���� �迭
* Returns    : int (0: ���� ����, -1: ����)
*****************************************************************************/
int main(int argc, char **argv)
{
    struct lws_context_creation_info info;
    struct ws_context context;
    int i;
    struct lws_vhost *vhost;
    
    // ���ؽ�Ʈ �ʱ�ȭ
    FD_ZERO(&context.read_set);
    context.max_fd = 0;
    
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        context.sessions[i].in_use = 0;
        context.sessions[i].fd = -1;
        context.sessions[i].buffer_len = 0;
        context.sessions[i].total_bytes = 0;
        context.sessions[i].record_count = 0;
    }
    
    // libwebsockets ���ؽ�Ʈ ����
    memset(&info, 0, sizeof(info));
    info.port = 8331;
    info.protocols = protocols;
    info.user = &context; // ����� ���ؽ�Ʈ ����
    
    context.lws_context = lws_create_context(&info);
    if (!context.lws_context)
    {
        fprintf(stderr, "SERVER: context ���� ����\n");
        return -1;
    }
    
    // ���� ���� �������� - libwebsockets 3.0 �̻��� ������� ����
    // �⺻ vhost�� �����ͼ� ���� fd�� ����
    vhost = lws_get_vhost_by_name(context.lws_context, "default");
    if (!vhost) {
        fprintf(stderr, "SERVER: vhost �������� ����\n");
        lws_context_destroy(context.lws_context);
        return -1;
    }
    
    // select�� ���� ���� fd ���� (���� ��ü�� ���� ������ �� ���� ���)
    // libwebsockets�� ���������� �����ϹǷ�, ���� �۵��� �ʿ��� �ּҰ����� ����
    context.server_fd = 0;
    context.max_fd = 10; // �Ϲ������� ���� ������ ���� ��ȣ�� ����
    
    FD_SET(context.server_fd, &context.read_set);
    
    printf("SERVER: WebSocket ���� ��� �� (��Ʈ %d)...\n", info.port);
    
    // ���� ����
    while (1)
    {
        process_client_data(&context);
    }
    
    // ���� (��������� ����)
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (context.sessions[i].in_use)
        {
            close(context.sessions[i].fd);
        }
    }
    
    lws_context_destroy(context.lws_context);
    return 0;
}

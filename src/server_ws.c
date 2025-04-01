#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <libwebsockets.h>

#define MAX_PAYLOAD_SIZE 1024

// �� Ŭ���̾�Ʈ ���ǿ� ���� ������ ����ü
struct per_session_data
{
    size_t total_len;
    struct timeval start_time;
    struct timeval end_time;
    int started;
};

/*****************************************************************************
* Function   : callback_file
* Description: ���� ���� �� �ð� ������ �ݹ� �Լ�
* Parameters :
* Returns    :
******************************************************************************/
static int callback_file(struct lws *wsi,
                         enum lws_callback_reasons reason,
                         void *user,
                         void *in,
                         size_t len)
{
    struct per_session_data *pss = NULL;
    double diff = 0.0;

    pss = (struct per_session_data *)user;

    switch (reason)
    {
        case LWS_CALLBACK_ESTABLISHED:
        {
            pss->total_len = 0;
            pss->started = 0;
            printf("Ŭ���̾�Ʈ �����.\n");
            break;
        }

        case LWS_CALLBACK_RECEIVE:
        {
            if (!pss->started)
            {
                gettimeofday(&pss->start_time, NULL);
                pss->started = 1;
                printf("���� ���� ���� �ð� ��ϵ�.\n");
            }

            pss->total_len += len;
            break;
        }

        case LWS_CALLBACK_CLOSED:
        {
            if (pss->started)
            {
                gettimeofday(&pss->end_time, NULL);
                diff = (pss->end_time.tv_sec - pss->start_time.tv_sec) +
                          (pss->end_time.tv_usec - pss->start_time.tv_usec) / 1000000.0;
                printf("���� ���� �Ϸ�: �� %zu ����Ʈ ����, �ҿ� �ð�: %.6f ��\n", pss->total_len, diff);
            }
            else
            {
                printf("���� ����: ���� ������ ���۵��� �ʾҽ��ϴ�.\n");
            }
            break;
        }

        default:
        {
            break;
        }
    }

    return 0;
}

// ����� �������� ���� (�������� �̸�: "file-transfer")
static struct lws_protocols protocols[] =
{
    {
        "file-transfer",
        callback_file,
        sizeof(struct per_session_data),
        MAX_PAYLOAD_SIZE,
    },
    { NULL, NULL, 0, 0 }
};

/*****************************************************************************
* Function   : main
* Description: ������ ���� �ʱ�ȭ �� �̺�Ʈ ���� ����
* Parameters :
* Returns    :
******************************************************************************/
int main(void)
{
    struct lws_context_creation_info info;
    struct lws_context *context = NULL;

    memset(&info, 0, sizeof(info));

    info.port = 8331;
    info.protocols = protocols;

    context = lws_create_context(&info);
    if (context == NULL)
    {
        fprintf(stderr, "libwebsockets �ʱ�ȭ ����\n");
        return -1;
    }

    printf("������ ���� ���� ������ ��Ʈ %d���� ���۵�.\n", info.port);

    while (1)
    {
        lws_service(context, 1000);
    }

    lws_context_destroy(context);
    return 0;
}


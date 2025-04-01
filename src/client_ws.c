#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <libwebsockets.h>

#define BUF_SIZE 1024

static const char *g_file_to_send = NULL;
static struct lws *g_wsi = NULL;

struct per_session_data
{
    FILE *fp;
    int file_eof;
    size_t total_bytes;
};

/*****************************************************************************
* Function   : callback_file_client
* Description: ������ Ŭ���̾�Ʈ�� �̺�Ʈ�� ó���ϴ� �ݹ� �Լ�.
*              - ���� ���� �� ���� ���� �ʱ�ȭ.
*              - ���� ���� �� ���Ͽ��� �����͸� �о� ������ ����.
*              - ���� ���� �Ϸ� �� ���α׷� ����.
*              - ���� �� ���� �̺�Ʈ ó�� ����.
* Parameters : - struct lws                  *wsi   : ������ ���� �ε���.
*              - enum   lws_callback_reasons  reason: �߻��� �̺�Ʈ ����.
*              - void                        *user  : ���� ������ ������.
*              - void                        *in    : �޽��� ���� ������.
*              - size_t                       len   : �޽��� ����.
* Returns    : 0 (���� ó��), -1 (���� �߻� ��)
******************************************************************************/
static int callback_file_client(struct lws *wsi,
                                enum lws_callback_reasons reason,
                                void *user,
                                void *in,
                                size_t len)
{
    struct per_session_data *pss = NULL;
    unsigned char *buf = NULL;
    size_t n = 0;
    int m = 0;

    pss = (struct per_session_data *)user;

    switch (reason)
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            lwsl_user("CLIENT: ���� ������\n");

            pss->fp = NULL;
            pss->file_eof = 0;
            pss->total_bytes = 0;

            pss->fp = fopen(g_file_to_send, "rb");
            if (!pss->fp)
            {
                lwsl_err("CLIENT: ���� ���� ����: %s\n", g_file_to_send);
                return -1;
            }

            lws_callback_on_writable(wsi);
            break;
        }

        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            if (pss->file_eof)
            {
                lwsl_user("CLIENT: ���� ���� �Ϸ�, ���α׷� ����\n");
                lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
                exit(0);
            }

            buf = malloc(LWS_PRE + BUF_SIZE);
            if (!buf)
            {
                lwsl_err("CLIENT: �޸� �Ҵ� ����\n");
                return -1;
            }

            n = fread(buf + LWS_PRE, 1, BUF_SIZE, pss->fp);
            if (n == 0)
            {
                pss->file_eof = 1;
                free(buf);
                lwsl_user("CLIENT: ���� ���� ������\n");
                lws_callback_on_writable(wsi);
                break;
            }

            pss->total_bytes += n;

            m = lws_write(wsi, buf + LWS_PRE, n, LWS_WRITE_BINARY);
            free(buf);

            if (m < (int)n)
            {
                lwsl_err("CLIENT: ������ ���� ����\n");
                return -1;
            }

            lwsl_user("CLIENT: %zu ����Ʈ ����, ���� %zu ����Ʈ\n", n, pss->total_bytes);
            lws_callback_on_writable(wsi);
            break;
        }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            lwsl_err("CLIENT: ���� ���� �߻�\n");
            break;
        }

        case LWS_CALLBACK_CLOSED:
        {
            lwsl_user("CLIENT: ���� �����\n");
            if (pss->fp)
            {
                fclose(pss->fp);
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

/*****************************************************************************
* Structure  : protocols
* Description: ����� �������� ����.
******************************************************************************/
static struct lws_protocols protocols[] =
{
    {
        "file-transfer",
        callback_file_client,
        sizeof(struct per_session_data),
        BUF_SIZE,
    },
    { NULL, NULL, 0, 0 }
};

/*****************************************************************************
* Function   : main
* Description: Ŭ���̾�Ʈ ���ø����̼��� ������.
*              - ����(localhost:8331)�� �����ϰ�, ������ �о� ����.
*              - ���� �� ��� �̺�Ʈ�� libwebsockets�� ó��.
* Parameters : - int    argc : ���� ����
*              - char **argv : ���� �迭
* Returns    : 0 (���� ����), -1 (���� �߻� ��)
******************************************************************************/
int main(int argc, char **argv)
{
    struct lws_context_creation_info info;
    struct lws_context *context = NULL;
    struct lws_client_connect_info ccinfo;

    memset(&info, 0, sizeof(info));
    memset(&ccinfo, 0, sizeof(ccinfo));

    if (argc < 2)
    {
        fprintf(stderr, "����: %s <������ ���� ���>\n", argv[0]);
        return -1;
    }

    g_file_to_send = argv[1];

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = 0;

    context = lws_create_context(&info);
    if (!context)
    {
        lwsl_err("CLIENT: libwebsockets �ʱ�ȭ ����\n");
        return -1;
    }

    ccinfo.context = context;
    ccinfo.address = "127.0.0.1";
    ccinfo.port = 8331;
    ccinfo.path = "/";
    ccinfo.host = lws_canonical_hostname(context);
    ccinfo.origin = "origin";
    ccinfo.protocol = protocols[0].name;
    ccinfo.ssl_connection = 0;

    g_wsi = lws_client_connect_via_info(&ccinfo);
    if (g_wsi == NULL)
    {
        lwsl_err("CLIENT: ���� ���� ����\n");
        lws_context_destroy(context);
        return -1;
    }

    while (lws_service(context, 1000) >= 0);

    lws_context_destroy(context);
    return 0;
}


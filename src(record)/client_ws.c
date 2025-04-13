/*****************************************************************************
* File       : client_ws_optimized.c
* Description: ��뷮 ���� ������ ���� WebSocket Ŭ���̾�Ʈ (������Ʈ + ���� ���� ���� ������)
*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libwebsockets.h>

#define BUF_SIZE 2048

static const char *g_file_to_send = NULL;
static struct lws *g_wsi = NULL;
static volatile int force_exit = 0;
static struct lws_context *g_ctx = NULL;

/*****************************************************************************
* Structure  : per_session_data
* Description: ���Ǻ� ����� ������ ����ü
*****************************************************************************/
struct per_session_data
{
    FILE *fp;
    int file_eof;
    size_t total_bytes;
    int retry_pending;
    char *retry_line;
    size_t retry_len;
};

/*****************************************************************************
* Function   : callback_file_client
* Description: WebSocket Ŭ���̾�Ʈ �ݹ� �Լ�
*****************************************************************************/
static int callback_file_client(struct lws *wsi,
                                enum lws_callback_reasons reason,
                                void *user,
                                void *in,
                                size_t len)
{
    struct per_session_data *pss = (struct per_session_data *)user;
    unsigned char buf[LWS_PRE + BUF_SIZE];
    char line[BUF_SIZE];
    size_t n = 0;
    int m = 0;

    switch (reason)
    {
        case LWS_CALLBACK_CLIENT_ESTABLISHED:
        {
            printf("[DEBUG] CLIENT: ���� ������\n");

            pss->fp = fopen(g_file_to_send, "r");
            if (!pss->fp)
            {
                fprintf(stderr, "[ERROR] CLIENT: ���� ���� ����: %s\n", g_file_to_send);
                return -1;
            }

            pss->file_eof = 0;
            pss->total_bytes = 0;
            pss->retry_pending = 0;
            pss->retry_line = NULL;
            pss->retry_len = 0;

            printf("[DEBUG] CLIENT: ���� ���� ����, ���� �غ� �Ϸ�\n");
            lws_callback_on_writable(wsi);
            break;
        }

        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            if (pss->retry_pending)
            {
                printf("[DEBUG] CLIENT: ������ �� (����: %zu)\n", pss->retry_len);
                if (pss->retry_line)
                {
                    memcpy(buf + LWS_PRE, pss->retry_line, pss->retry_len);
                    m = lws_write(wsi, buf + LWS_PRE, pss->retry_len, LWS_WRITE_TEXT);
                    if (m == -1)
                    {
                        fprintf(stderr, "[ERROR] CLIENT: ������ ���� (-1/%zu)\n", pss->retry_len);
                        return 0;
                    }
                    else if (m < (int)pss->retry_len)
                    {
                        fprintf(stderr, "[ERROR] CLIENT: ������ �� �κ� ���� (%d/%zu), ����\n", m, pss->retry_len);
                        return -1;
                    }

                    printf("[DEBUG] CLIENT: ������ ���� (%zu ����Ʈ)\n", pss->retry_len);
                    free(pss->retry_line);
                    pss->retry_line = NULL;
                    pss->retry_len = 0;
                    pss->retry_pending = 0;
                }
            }
            else if (!pss->file_eof && fgets(line, sizeof(line), pss->fp))
            {
                n = strlen(line);
                if (n > BUF_SIZE - LWS_PRE)
                {
                    fprintf(stderr, "[ERROR] CLIENT: ������ ũ�� �ʰ� (%zu ����Ʈ), ���� �ߴ�\n", n);
                    return -1;
                }

                pss->total_bytes += n;

                memcpy(buf + LWS_PRE, line, n);
                m = lws_write(wsi, buf + LWS_PRE, n, LWS_WRITE_TEXT);
                if (m == -1)
                {
                    fprintf(stderr, "[ERROR] CLIENT: ���� ���� (-1/%zu), ��õ� ���\n", n);
                    pss->retry_line = strdup(line);
                    pss->retry_len = n;
                    pss->retry_pending = 1;
                    return 0;
                }
                else if (m < (int)n)
                {
                    fprintf(stderr, "[ERROR] CLIENT: �κ� ���� (%d/%zu), ����\n", m, n);
                    return -1;
                }

            }
            else if (!pss->file_eof)
            {
                pss->file_eof = 1;
                if (pss->fp)
                {
                    fclose(pss->fp);
                    pss->fp = NULL;
                }

            }

            if (pss->file_eof && !pss->retry_pending)
            {
                printf("[DEBUG] CLIENT: ��� ���ڵ� ���� �Ϸ�. ���� ���� ��û\n");
                lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
                return -1;
            }

            lws_callback_on_writable(wsi);
            break;
        }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            fprintf(stderr, "[ERROR] CLIENT: ���� ���� �Ǵ� ����\n");
            force_exit = 1;
            lws_cancel_service(g_ctx);
            break;
        }

        case LWS_CALLBACK_CLOSED:
        {
            printf("[DEBUG] CLIENT: ���� �����\n");
            force_exit = 1;
            lws_cancel_service(g_ctx);
            break;
        }

        default:
            break;
    }

    return 0;
}

/*****************************************************************************
* Structure  : protocols
* Description: ����� �������� ����
*****************************************************************************/
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
* Description: WebSocket Ŭ���̾�Ʈ ������
*****************************************************************************/
int main(int argc, char **argv)
{
    struct lws_context_creation_info info;
    struct lws_client_connect_info ccinfo;

    if (argc < 2)
    {
        fprintf(stderr, "����: %s <������ ���� ���>\n", argv[0]);
        return -1;
    }

    g_file_to_send = argv[1];

    memset(&info, 0, sizeof(info));
    memset(&ccinfo, 0, sizeof(ccinfo));

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;

    g_ctx = lws_create_context(&info);
    if (!g_ctx)
    {
        fprintf(stderr, "CLIENT: context ���� ����\n");
        return -1;
    }

    ccinfo.context = g_ctx;
    ccinfo.address = "127.0.0.1";
    ccinfo.port = 8331;
    ccinfo.path = "/";
    ccinfo.host = lws_canonical_hostname(g_ctx);
    ccinfo.origin = "origin";
    ccinfo.protocol = "file-transfer";
    ccinfo.ssl_connection = 0;

    g_wsi = lws_client_connect_via_info(&ccinfo);
    if (!g_wsi)
    {
        fprintf(stderr, "CLIENT: ���� ���� ����\n");
        lws_context_destroy(g_ctx);
        return -1;
    }

    while (!force_exit)
    {
        if (lws_service(g_ctx, 100) < 0)
        {
            break;
        }
    }

    lws_context_destroy(g_ctx);
    printf("CLIENT: ���α׷� ���� ����\n");
    return 0;
}

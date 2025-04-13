/*****************************************************************************
* File       : client_ws_optimized.c
* Description: 대용량 안전 전송을 위한 WebSocket 클라이언트 (세그폴트 + 전송 실패 대응 완전판)
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
* Description: 세션별 사용자 데이터 구조체
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
* Description: WebSocket 클라이언트 콜백 함수
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
            printf("[DEBUG] CLIENT: 연결 성립됨\n");

            pss->fp = fopen(g_file_to_send, "r");
            if (!pss->fp)
            {
                fprintf(stderr, "[ERROR] CLIENT: 파일 열기 실패: %s\n", g_file_to_send);
                return -1;
            }

            pss->file_eof = 0;
            pss->total_bytes = 0;
            pss->retry_pending = 0;
            pss->retry_line = NULL;
            pss->retry_len = 0;

            printf("[DEBUG] CLIENT: 파일 열기 성공, 전송 준비 완료\n");
            lws_callback_on_writable(wsi);
            break;
        }

        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            if (pss->retry_pending)
            {
                printf("[DEBUG] CLIENT: 재전송 중 (길이: %zu)\n", pss->retry_len);
                if (pss->retry_line)
                {
                    memcpy(buf + LWS_PRE, pss->retry_line, pss->retry_len);
                    m = lws_write(wsi, buf + LWS_PRE, pss->retry_len, LWS_WRITE_TEXT);
                    if (m == -1)
                    {
                        fprintf(stderr, "[ERROR] CLIENT: 재전송 실패 (-1/%zu)\n", pss->retry_len);
                        return 0;
                    }
                    else if (m < (int)pss->retry_len)
                    {
                        fprintf(stderr, "[ERROR] CLIENT: 재전송 중 부분 전송 (%d/%zu), 종료\n", m, pss->retry_len);
                        return -1;
                    }

                    printf("[DEBUG] CLIENT: 재전송 성공 (%zu 바이트)\n", pss->retry_len);
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
                    fprintf(stderr, "[ERROR] CLIENT: 데이터 크기 초과 (%zu 바이트), 전송 중단\n", n);
                    return -1;
                }

                pss->total_bytes += n;

                memcpy(buf + LWS_PRE, line, n);
                m = lws_write(wsi, buf + LWS_PRE, n, LWS_WRITE_TEXT);
                if (m == -1)
                {
                    fprintf(stderr, "[ERROR] CLIENT: 전송 실패 (-1/%zu), 재시도 등록\n", n);
                    pss->retry_line = strdup(line);
                    pss->retry_len = n;
                    pss->retry_pending = 1;
                    return 0;
                }
                else if (m < (int)n)
                {
                    fprintf(stderr, "[ERROR] CLIENT: 부분 전송 (%d/%zu), 종료\n", m, n);
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
                printf("[DEBUG] CLIENT: 모든 레코드 전송 완료. 연결 종료 요청\n");
                lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
                return -1;
            }

            lws_callback_on_writable(wsi);
            break;
        }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            fprintf(stderr, "[ERROR] CLIENT: 연결 실패 또는 에러\n");
            force_exit = 1;
            lws_cancel_service(g_ctx);
            break;
        }

        case LWS_CALLBACK_CLOSED:
        {
            printf("[DEBUG] CLIENT: 연결 종료됨\n");
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
* Description: 사용할 프로토콜 정의
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
* Description: WebSocket 클라이언트 진입점
*****************************************************************************/
int main(int argc, char **argv)
{
    struct lws_context_creation_info info;
    struct lws_client_connect_info ccinfo;

    if (argc < 2)
    {
        fprintf(stderr, "사용법: %s <전송할 파일 경로>\n", argv[0]);
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
        fprintf(stderr, "CLIENT: context 생성 실패\n");
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
        fprintf(stderr, "CLIENT: 서버 연결 실패\n");
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
    printf("CLIENT: 프로그램 정상 종료\n");
    return 0;
}

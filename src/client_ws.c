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
* Description: 웹소켓 클라이언트의 이벤트를 처리하는 콜백 함수.
*              - 연결 성공 시 파일 열고 초기화.
*              - 쓰기 가능 시 파일에서 데이터를 읽어 서버로 전송.
*              - 파일 전송 완료 시 프로그램 종료.
*              - 에러 및 종료 이벤트 처리 포함.
* Parameters : - struct lws                  *wsi   : 웹소켓 세션 인덱스.
*              - enum   lws_callback_reasons  reason: 발생한 이벤트 종류.
*              - void                        *user  : 세션 데이터 포인터.
*              - void                        *in    : 메시지 버퍼 포인터.
*              - size_t                       len   : 메시지 길이.
* Returns    : 0 (정상 처리), -1 (에러 발생 시)
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
            lwsl_user("CLIENT: 연결 성립됨\n");

            pss->fp = NULL;
            pss->file_eof = 0;
            pss->total_bytes = 0;

            pss->fp = fopen(g_file_to_send, "rb");
            if (!pss->fp)
            {
                lwsl_err("CLIENT: 파일 열기 실패: %s\n", g_file_to_send);
                return -1;
            }

            lws_callback_on_writable(wsi);
            break;
        }

        case LWS_CALLBACK_CLIENT_WRITEABLE:
        {
            if (pss->file_eof)
            {
                lwsl_user("CLIENT: 파일 전송 완료, 프로그램 종료\n");
                lws_close_reason(wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
                exit(0);
            }

            buf = malloc(LWS_PRE + BUF_SIZE);
            if (!buf)
            {
                lwsl_err("CLIENT: 메모리 할당 실패\n");
                return -1;
            }

            n = fread(buf + LWS_PRE, 1, BUF_SIZE, pss->fp);
            if (n == 0)
            {
                pss->file_eof = 1;
                free(buf);
                lwsl_user("CLIENT: 파일 끝에 도달함\n");
                lws_callback_on_writable(wsi);
                break;
            }

            pss->total_bytes += n;

            m = lws_write(wsi, buf + LWS_PRE, n, LWS_WRITE_BINARY);
            free(buf);

            if (m < (int)n)
            {
                lwsl_err("CLIENT: 데이터 전송 오류\n");
                return -1;
            }

            lwsl_user("CLIENT: %zu 바이트 전송, 누적 %zu 바이트\n", n, pss->total_bytes);
            lws_callback_on_writable(wsi);
            break;
        }

        case LWS_CALLBACK_CLIENT_CONNECTION_ERROR:
        {
            lwsl_err("CLIENT: 연결 에러 발생\n");
            break;
        }

        case LWS_CALLBACK_CLOSED:
        {
            lwsl_user("CLIENT: 연결 종료됨\n");
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
* Description: 사용할 프로토콜 정의.
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
* Description: 클라이언트 애플리케이션의 진입점.
*              - 서버(localhost:8331)에 연결하고, 파일을 읽어 전송.
*              - 연결 및 통신 이벤트는 libwebsockets가 처리.
* Parameters : - int    argc : 인자 개수
*              - char **argv : 인자 배열
* Returns    : 0 (정상 종료), -1 (오류 발생 시)
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
        fprintf(stderr, "사용법: %s <전송할 파일 경로>\n", argv[0]);
        return -1;
    }

    g_file_to_send = argv[1];

    info.port = CONTEXT_PORT_NO_LISTEN;
    info.protocols = protocols;
    info.options = 0;

    context = lws_create_context(&info);
    if (!context)
    {
        lwsl_err("CLIENT: libwebsockets 초기화 실패\n");
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
        lwsl_err("CLIENT: 서버 연결 실패\n");
        lws_context_destroy(context);
        return -1;
    }

    while (lws_service(context, 1000) >= 0);

    lws_context_destroy(context);
    return 0;
}


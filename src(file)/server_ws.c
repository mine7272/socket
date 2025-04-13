#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <libwebsockets.h>

#define MAX_PAYLOAD_SIZE 1024

// 각 클라이언트 세션에 대한 데이터 구조체
struct per_session_data
{
    size_t total_len;
    struct timeval start_time;
    struct timeval end_time;
    int started;
};

/*****************************************************************************
* Function   : callback_file
* Description: 파일 수신 및 시간 측정용 콜백 함수
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
            printf("클라이언트 연결됨.\n");
            break;
        }

        case LWS_CALLBACK_RECEIVE:
        {
            if (!pss->started)
            {
                gettimeofday(&pss->start_time, NULL);
                pss->started = 1;
                printf("파일 전송 시작 시간 기록됨.\n");
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
                printf("파일 전송 완료: 총 %zu 바이트 수신, 소요 시간: %.6f 초\n", pss->total_len, diff);
            }
            else
            {
                printf("연결 종료: 파일 전송이 시작되지 않았습니다.\n");
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

// 사용할 프로토콜 설정 (프로토콜 이름: "file-transfer")
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
* Description: 웹소켓 서버 초기화 및 이벤트 루프 실행
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
        fprintf(stderr, "libwebsockets 초기화 실패\n");
        return -1;
    }

    printf("웹소켓 파일 수신 서버가 포트 %d에서 시작됨.\n", info.port);

    while (1)
    {
        lws_service(context, 1000);
    }

    lws_context_destroy(context);
    return 0;
}


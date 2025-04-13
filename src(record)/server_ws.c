/*****************************************************************************
* File       : server_ws_record.c
* Description: WebSocket 서버 - \n 단위 레코드 수신 + 로그 + 수신 시간 측정 (select 기반 멀티플렉싱)
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
* Description: 세션별 사용자 데이터 및 수신 통계
*****************************************************************************/
struct per_session_data
{
    char buffer[BUF_SIZE];
    size_t buffer_len;
    size_t total_bytes;
    int record_count;
    struct timeval start_time;
    int in_use;                   // 세션 사용 중 여부
    int fd;                       // 소켓 파일 디스크립터
};

/*****************************************************************************
* Structure  : ws_context
* Description: WebSocket 서버 컨텍스트 데이터
*****************************************************************************/
struct ws_context
{
    struct per_session_data sessions[MAX_CLIENTS];
    fd_set read_set;
    int max_fd;
    int server_fd;
    struct lws_context *lws_context; // libwebsockets 컨텍스트 참조 저장
};

/*****************************************************************************
* Function   : find_free_session
* Description: 사용 가능한 세션 인덱스 찾기
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
* Description: 새 클라이언트 연결 처리
*****************************************************************************/
static void handle_established(struct ws_context *context, int client_fd)
{
    int session_idx = find_free_session(context);
    
    if (session_idx < 0)
    {
        fprintf(stderr, "SERVER: 최대 클라이언트 연결 수 초과\n");
        close(client_fd);
        return;
    }
    
    struct per_session_data *pss = &context->sessions[session_idx];
    
    printf("SERVER: 클라이언트 연결됨\n");
    pss->buffer_len = 0;
    pss->total_bytes = 0;
    pss->record_count = 0;
    pss->in_use = 1;
    pss->fd = client_fd;
    gettimeofday(&pss->start_time, NULL);
    
    // 소켓 추가
    FD_SET(client_fd, &context->read_set);
    if (client_fd > context->max_fd)
    {
        context->max_fd = client_fd;
    }
}

/*****************************************************************************
* Function   : handle_receive
* Description: 클라이언트로부터 데이터 수신 처리
*****************************************************************************/
static int handle_receive(struct per_session_data *pss, char *in, size_t len)
{
    char *start = NULL;
    char *end = NULL;
    size_t remain = 0;
    
    if (pss->buffer_len + len >= BUF_SIZE)
    {
        fprintf(stderr, "SERVER: 버퍼 초과\n");
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
* Description: 클라이언트 연결 종료 처리
*****************************************************************************/
static void handle_close(struct ws_context *context, struct per_session_data *pss)
{
    struct timeval end_time;
    double elapsed = 0.0;
    
    gettimeofday(&end_time, NULL);
    elapsed = (end_time.tv_sec - pss->start_time.tv_sec) +
              (end_time.tv_usec - pss->start_time.tv_usec) / 1000000.0;
    
    printf("SERVER: 연결 종료됨\n");
    printf("SERVER: 총 수신 바이트: %zu, 레코드 수: %d, 소요 시간: %.6f 초\n",
            pss->total_bytes, pss->record_count, elapsed);
    
    // 소켓 제거
    FD_CLR(pss->fd, &context->read_set);
    close(pss->fd);
    
    // 세션 정리
    pss->buffer_len = 0;
    pss->total_bytes = 0;
    pss->record_count = 0;
    pss->in_use = 0;
    pss->fd = -1;
}

/*****************************************************************************
* Function   : callback_server
* Description: WebSocket 서버 콜백 함수
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
* Description: 사용할 WebSocket 프로토콜 정의
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
* Description: 새 클라이언트 연결 수락
*****************************************************************************/
static void accept_new_connection(struct ws_context *context)
{
    // libwebsockets 이벤트 처리 실행
    lws_service(context->lws_context, 0);
}

/*****************************************************************************
* Function   : process_client_data
* Description: 클라이언트 데이터 처리
*****************************************************************************/
static void process_client_data(struct ws_context *context)
{
    fd_set read_fds;
    struct timeval tv;
    int activity;
    
    // select 준비
    read_fds = context->read_set;
    tv.tv_sec = 0;
    tv.tv_usec = 10000; // 10ms
    
    activity = select(context->max_fd + 1, &read_fds, NULL, NULL, &tv);
    
    if (activity < 0)
    {
        perror("SERVER: select 오류");
        return;
    }
    
    // 활동이 있으면 libwebsockets에 처리 요청
    if (activity > 0) {
        accept_new_connection(context);
    } else {
        // 타임아웃 시에도 libwebsockets 서비스 호출
        lws_service(context->lws_context, 0);
    }
}

/*****************************************************************************
* Function   : main
* Description: WebSocket 서버 진입점, 수신 루프 실행
* Parameters : - int argc     : 인자 개수
*              - char **argv  : 인자 배열
* Returns    : int (0: 정상 종료, -1: 오류)
*****************************************************************************/
int main(int argc, char **argv)
{
    struct lws_context_creation_info info;
    struct ws_context context;
    int i;
    struct lws_vhost *vhost;
    
    // 컨텍스트 초기화
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
    
    // libwebsockets 컨텍스트 설정
    memset(&info, 0, sizeof(info));
    info.port = 8331;
    info.protocols = protocols;
    info.user = &context; // 사용자 컨텍스트 설정
    
    context.lws_context = lws_create_context(&info);
    if (!context.lws_context)
    {
        fprintf(stderr, "SERVER: context 생성 실패\n");
        return -1;
    }
    
    // 서버 소켓 가져오기 - libwebsockets 3.0 이상의 방식으로 수정
    // 기본 vhost를 가져와서 서버 fd를 설정
    vhost = lws_get_vhost_by_name(context.lws_context, "default");
    if (!vhost) {
        fprintf(stderr, "SERVER: vhost 가져오기 실패\n");
        lws_context_destroy(context.lws_context);
        return -1;
    }
    
    // select를 위한 서버 fd 설정 (소켓 자체를 직접 가져올 수 없는 경우)
    // libwebsockets가 내부적으로 관리하므로, 서버 작동에 필요한 최소값으로 설정
    context.server_fd = 0;
    context.max_fd = 10; // 일반적으로 서버 소켓은 낮은 번호를 가짐
    
    FD_SET(context.server_fd, &context.read_set);
    
    printf("SERVER: WebSocket 수신 대기 중 (포트 %d)...\n", info.port);
    
    // 메인 루프
    while (1)
    {
        process_client_data(&context);
    }
    
    // 정리 (실행되지는 않음)
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

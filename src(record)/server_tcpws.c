/*****************************************************************************
* File       : server_tcpws.c
* Description: TCP 및 WebSocket 프로토콜을 처리하는 서버 프로그램 (select 기반 멀티플렉싱)
*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/select.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#define PORT 8331
#define BUF_SIZE 2048
#define MAX_RECV_BUF 102400
#define MAX_CLIENTS 30

/*****************************************************************************
* Structure  : client_data
* Description: 클라이언트 연결별 데이터 저장
*****************************************************************************/
struct client_data
{
    int fd;                             // 클라이언트 소켓 파일 디스크립터
    int is_websocket;                   // WebSocket 연결 여부
    unsigned char recv_buf[MAX_RECV_BUF]; // 수신 버퍼
    size_t recv_buf_len;                // 수신 버퍼에 저장된 데이터 길이
    unsigned char *all_data;            // 전체 수신 데이터
    size_t total_len;                   // 전체 수신 데이터 길이
    size_t capacity;                    // 할당된 버퍼 크기
    size_t record_count;                // 수신한 레코드 수 (줄 바꿈 기준)
    struct timeval start_time;          // 수신 시작 시간
    int handshake_completed;            // WebSocket 핸드셰이크 완료 여부
};

/*****************************************************************************
* Function   : base64_encode
* Description: 바이너리 데이터를 base64로 인코딩
*****************************************************************************/
char* base64_encode(const unsigned char *input, int length)
{
    BIO *bmem = NULL;
    BIO *b64 = NULL;
    BUF_MEM *bptr = NULL;
    char *buff = NULL;

    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);

    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, input, length);
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);

    buff = (char *)malloc(bptr->length + 1);
    memcpy(buff, bptr->data, bptr->length);
    buff[bptr->length] = '\0';

    BIO_free_all(b64);

    return buff;
}

/*****************************************************************************
* Function   : compute_accept_key
* Description: WebSocket 핸드셰이크용 Accept 키 생성
*****************************************************************************/
char* compute_accept_key(const char *client_key)
{
    const char *GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char concatenated[256];
    unsigned char hash[SHA_DIGEST_LENGTH];

    snprintf(concatenated, sizeof(concatenated), "%s%s", client_key, GUID);
    SHA1((unsigned char*)concatenated, strlen(concatenated), hash);

    return base64_encode(hash, SHA_DIGEST_LENGTH);
}

/*****************************************************************************
* Function   : extract_websocket_key
* Description: 요청 헤더에서 Sec-WebSocket-Key 추출
*****************************************************************************/
char* extract_websocket_key(const char *request)
{
    const char *key_header = "Sec-WebSocket-Key: ";
    char *key_start = NULL;
    char *key_end = NULL;
    char *key = NULL;
    size_t key_len = 0;

    key_start = strstr(request, key_header);
    if (key_start == 0)
        return NULL;

    key_start += strlen(key_header);
    key_end = strstr(key_start, "\r\n");
    if (key_end == 0) 
        return NULL;

    key_len = key_end - key_start;
    key = malloc(key_len + 1);
    strncpy(key, key_start, key_len);
    key[key_len] = '\0';

    return key;
}

/*****************************************************************************
* Function   : decode_ws_frame
* Description: WebSocket 프레임을 디코드하고 마스킹 제거 (불완전 프레임 대기 처리 포함)
*****************************************************************************/
int decode_ws_frame(const unsigned char *frame, size_t length, unsigned char *output, size_t *frame_len_out)
{
    size_t payload_len = 0;
    size_t offset = 0;
    size_t i = 0;
    const unsigned char *mask_key = NULL;
    const unsigned char *payload = NULL;

    if (length < 6)
        return 0; // 프레임 헤더조차 부족함 → 대기

    payload_len = frame[1] & 0x7F;
    offset = 2;

    if (payload_len == 126)
    {
        if (length < 4)
            return 0;

        payload_len = (frame[2] << 8) | frame[3];
        offset += 2;
    }
    else if (payload_len == 127)
    {
        if (length < 10)
            return 0;

        payload_len = 0;
        for (i = 0; i < 8; i++)
        {
            payload_len |= ((size_t)frame[offset + i]) << (8 * (7 - i));
        }
        offset += 8;
    }

    if (length < offset + 4 + payload_len)
        return 0; // 데이터가 아직 완전히 수신되지 않음 → 대기

    mask_key = frame + offset;
    payload = frame + offset + 4;

    for (i = 0; i < payload_len; i++)
    {
        output[i] = payload[i] ^ mask_key[i % 4];
    }

    if (frame_len_out)
    {
        *frame_len_out = offset + 4 + payload_len;
    }

    return (int)payload_len;
}

/*****************************************************************************
* Function   : handle_new_connection
* Description: 새 클라이언트 연결 처리
*****************************************************************************/
int handle_new_connection(int server_fd, fd_set *master_set, int *max_fd, struct client_data *clients)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    int i;
    
    if (client_fd < 0)
    {
        perror("accept 실패");
        return -1;
    }
    
    printf("클라이언트 연결됨\n");
    
    // 빈 슬롯 찾기
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd == -1)
        {
            clients[i].fd = client_fd;
            clients[i].is_websocket = 0;
            clients[i].recv_buf_len = 0;
            clients[i].handshake_completed = 0;
            clients[i].total_len = 0;
            clients[i].record_count = 0;
            clients[i].capacity = 102400;
            clients[i].all_data = malloc(clients[i].capacity);
            
            if (clients[i].all_data == NULL)
            {
                perror("메모리 할당 실패");
                close(client_fd);
                clients[i].fd = -1;
                return -1;
            }
            
            gettimeofday(&clients[i].start_time, NULL);
            
            FD_SET(client_fd, master_set);
            if (client_fd > *max_fd)
            {
                *max_fd = client_fd;
            }
            
            return 0;
        }
    }
    
    printf("최대 클라이언트 연결 수 초과\n");
    close(client_fd);
    return -1;
}

/*****************************************************************************
* Function   : handle_websocket_data
* Description: WebSocket 데이터 처리
*****************************************************************************/
void handle_websocket_data(struct client_data *client, char *buffer, size_t recv_len)
{
    char data[BUF_SIZE];
    size_t offset = 0;
    size_t frame_len = 0;
    int data_len = 0;
    size_t i = 0;
    
    if (client->recv_buf_len + recv_len > MAX_RECV_BUF)
    {
        fprintf(stderr, "버퍼 초과. 수신 중단\n");
        return;
    }
    
    memcpy(client->recv_buf + client->recv_buf_len, buffer, recv_len);
    client->recv_buf_len += recv_len;
    
    offset = 0;
    while (offset < client->recv_buf_len)
    {
        frame_len = 0;
        data_len = decode_ws_frame(client->recv_buf + offset, client->recv_buf_len - offset, 
                                  (unsigned char*)data, &frame_len);
        
        if (data_len > 0 && frame_len > 0)
        {
            if (client->total_len + data_len > client->capacity)
            {
                client->capacity *= 2;
                client->all_data = realloc(client->all_data, client->capacity);
                if (client->all_data == NULL)
                {
                    fprintf(stderr, "메모리 재할당 실패\n");
                    return;
                }
            }
            
            memcpy(client->all_data + client->total_len, data, data_len);
            for (i = 0; i < data_len; i++)
                if (data[i] == '\n') client->record_count++;
            
            client->total_len += data_len;
            offset += frame_len;
        }
        else if (frame_len == 0)
        {
            break;
        }
        else
        {
            fprintf(stderr, "프레임 디코딩 실패 %d %zu\n", data_len, frame_len);
            break;
        }
    }
    
    if (offset > 0 && offset < client->recv_buf_len)
    {
        memmove(client->recv_buf, client->recv_buf + offset, client->recv_buf_len - offset);
        client->recv_buf_len -= offset;
    }
    else if (offset == client->recv_buf_len)
    {
        client->recv_buf_len = 0;
    }
}

/*****************************************************************************
* Function   : handle_tcp_data
* Description: 일반 TCP 데이터 처리
*****************************************************************************/
void handle_tcp_data(struct client_data *client, char *buffer, size_t recv_len)
{
    size_t i = 0;
    
    if (client->total_len + recv_len > client->capacity)
    {
        client->capacity *= 2;
        client->all_data = realloc(client->all_data, client->capacity);
        if (client->all_data == NULL)
        {
            fprintf(stderr, "메모리 재할당 실패\n");
            return;
        }
    }
    
    memcpy(client->all_data + client->total_len, buffer, recv_len);
    for (i = 0; i < recv_len; i++)
        if (buffer[i] == '\n') client->record_count++;
    
    client->total_len += recv_len;
}

/*****************************************************************************
* Function   : handle_client_data
* Description: 클라이언트 데이터 수신 및 처리
*****************************************************************************/
void handle_client_data(struct client_data *client, fd_set *master_set)
{
    char buffer[BUF_SIZE];
    size_t recv_len = 0;
    char *client_key = NULL;
    char *accept_key = NULL;
    char response[512];
    struct timeval end_time;
    double diff = 0.0;
    
    recv_len = recv(client->fd, buffer, BUF_SIZE - 1, 0);
    if (recv_len <= 0)
    {
        if (recv_len == 0)
        {
            // 연결 종료
            gettimeofday(&end_time, NULL);
            diff = (end_time.tv_sec - client->start_time.tv_sec) + 
                   (end_time.tv_usec - client->start_time.tv_usec) / 1000000.0;
            
            if (client->is_websocket)
            {
                printf("[WS] 총 수신 바이트: %zu, 레코드 수: %zu, 소요 시간: %.6f 초\n", 
                       client->total_len, client->record_count, diff);
            }
            else
            {
                printf("[TCP] 총 수신 바이트: %zu, 레코드 수: %zu, 소요 시간: %.6f 초\n", 
                       client->total_len, client->record_count, diff);
            }
            
            printf("클라이언트 연결 종료\n\n");
        }
        else
        {
            perror("recv 실패");
        }
        
        free(client->all_data);
        close(client->fd);
        FD_CLR(client->fd, master_set);
        client->fd = -1;
        return;
    }
    
    buffer[recv_len] = '\0';
    
    // 초기 연결 확인 (WebSocket 핸드셰이크 여부 판단)
    if (!client->is_websocket && !client->handshake_completed && strncmp(buffer, "GET", 3) == 0)
    {
        client->is_websocket = 1;
        client_key = extract_websocket_key(buffer);
        if (client_key != NULL)
        {
            accept_key = compute_accept_key(client_key);
            snprintf(response, sizeof(response),
                     "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n\r\n", accept_key);
            
            send(client->fd, response, strlen(response), 0);
            free(client_key);
            free(accept_key);
            
            client->handshake_completed = 1;
            printf("[WS] handshake 완료. 수신 시작\n");
            gettimeofday(&client->start_time, NULL);
        }
        else
        {
            fprintf(stderr, "WebSocket 키 추출 실패\n");
            close(client->fd);
            FD_CLR(client->fd, master_set);
            client->fd = -1;
        }
    }
    else if (client->is_websocket && client->handshake_completed)
    {
        handle_websocket_data(client, buffer, recv_len);
    }
    else
    {
        handle_tcp_data(client, buffer, recv_len);
    }
}

/*****************************************************************************
* Function   : main
* Description: TCP 및 WebSocket 서버 실행 루틴
*****************************************************************************/
int main()
{
    int server_fd = 0;
    struct sockaddr_in server_addr;
    int i, select_result, fd;
    
    fd_set master_set, working_set;
    int max_fd;
    struct client_data clients[MAX_CLIENTS];
    
    // 클라이언트 배열 초기화
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].fd = -1;
        clients[i].all_data = NULL;
    }
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("소켓 생성 실패");
        return -1;
    }
    
    // SO_REUSEADDR 옵션 설정
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt 실패");
        close(server_fd);
        return -1;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind 실패");
        close(server_fd);
        return -1;
    }
    
    if (listen(server_fd, 10) < 0)
    {
        perror("listen 실패");
        close(server_fd);
        return -1;
    }
    
    printf("서버 실행 중 (포트 %d)...\n", PORT);
    
    // select 초기화
    FD_ZERO(&master_set);
    FD_SET(server_fd, &master_set);
    max_fd = server_fd;
    
    struct timeval timeout;
    
    while (1)
    {
        working_set = master_set;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        select_result = select(max_fd + 1, &working_set, NULL, NULL, &timeout);
        
        if (select_result < 0)
        {
            perror("select 실패");
            break;
        }
        
        if (select_result == 0)
        {
            // 타임아웃, 필요시 추가 작업 수행
            continue;
        }
        
        for (fd = 0; fd <= max_fd; fd++)
        {
            if (FD_ISSET(fd, &working_set))
            {
                if (fd == server_fd)
                {
                    // 새 연결 요청
                    handle_new_connection(server_fd, &master_set, &max_fd, clients);
                }
                else
                {
                    // 클라이언트 데이터 처리
                    for (i = 0; i < MAX_CLIENTS; i++)
                    {
                        if (clients[i].fd == fd)
                        {
                            handle_client_data(&clients[i], &master_set);
                            break;
                        }
                    }
                }
            }
        }
    }
    
    // 정리
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].fd != -1)
        {
            close(clients[i].fd);
            free(clients[i].all_data);
        }
    }
    
    close(server_fd);
    return 0;
}

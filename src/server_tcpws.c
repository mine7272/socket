#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#define PORT 8331
#define BUF_SIZE 2048

/*****************************************************************************
* Function   : base64_encode
* Description: SHA1 결과를 Base64 인코딩
* Parameters : - const unsigned char *input : 입력 데이터
*             - int length : 길이
* Returns    : Base64 문자열 (free 필요)
******************************************************************************/
char* base64_encode(const unsigned char *input, int length)
{
    BIO *bmem, *b64;
    BUF_MEM *bptr;
    char *buff;

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
* Description: 클라이언트 WebSocket Key로 Accept Key 생성
* Parameters : - const char *client_key : 클라이언트 Key
* Returns    : Accept Key 문자열 (free 필요)
******************************************************************************/
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
* Description: HTTP 헤더에서 Sec-WebSocket-Key 추출
* Parameters : - const char *request : 전체 요청 문자열
* Returns    : Key 문자열 포인터 (free 필요)
******************************************************************************/
char* extract_websocket_key(const char *request)
{
    const char *key_header = "Sec-WebSocket-Key: ";
    char *key_start;
    char *key_end;
    size_t key_len;
    char *key;

    key_start = strstr(request, key_header);
    if (!key_start) return NULL;

    key_start += strlen(key_header);
    key_end = strstr(key_start, "\r\n");
    if (!key_end) return NULL;

    key_len = key_end - key_start;
    key = malloc(key_len + 1);
    strncpy(key, key_start, key_len);
    key[key_len] = '\0';
    return key;
}

/*****************************************************************************
* Function   : decode_ws_frame
* Description: WebSocket 프레임을 해석하고 마스킹 해제된 데이터 추출
* Parameters : - const unsigned char *frame : 수신 프레임
*             - size_t length : 프레임 전체 길이
*             - unsigned char *output : 결과 버퍼
* Returns    : payload 길이 (실패 시 -1)
******************************************************************************/
int decode_ws_frame(const unsigned char *frame, size_t length, unsigned char *output)
{
    size_t payload_len;
    size_t offset;
    const unsigned char *mask_key;
    const unsigned char *payload;
    size_t i;

    if (length < 6)
    {
        return -1;
    }

    payload_len = frame[1] & 0x7F;
    offset = 2;

    if (payload_len == 126)
    {
        if (length < 8)
        {
            return -1;
        }
        payload_len = (frame[2] << 8) | frame[3];
        offset += 2;
    }
    else if (payload_len == 127)
    {
        if (length < 14)
        {
            return -1;
        }
        payload_len = 0;
        for (i = 0; i < 8; i++)
        {
            payload_len |= ((size_t)frame[offset + i]) << (8 * (7 - i));
        }
        offset += 8;
    }

    if (length < offset + 4 + payload_len)
    {
        return -1;
    }

    mask_key = frame + offset;
    payload = frame + offset + 4;

    for (i = 0; i < payload_len; i++)
    {
        output[i] = payload[i] ^ mask_key[i % 4];
    }

    return (int)payload_len;
}


/*****************************************************************************
* Function   : main
* Description: WebSocket 서버 구현
* Parameters :
* Returns    : 0
******************************************************************************/
int main()
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    char buffer[BUF_SIZE];
    char data[BUF_SIZE];
    ssize_t recv_len;
    char *client_key;
    char *accept_key;
    char response[512];
    int data_len;
    int i;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("소켓 생성 실패");
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

    if (listen(server_fd, 1) < 0)
    {
        perror("listen 실패");
        close(server_fd);
        return -1;
    }

    printf("WebSocket 서버 실행 중 (포트 %d)...\n", PORT);

    client_len = sizeof(client_addr);
    client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0)
    {
        perror("accept 실패");
        close(server_fd);
        return -1;
    }

    recv_len = recv(client_fd, buffer, BUF_SIZE - 1, 0);
    buffer[recv_len] = '\0';
    printf("HTTP 요청:\n%s\n", buffer);

    client_key = extract_websocket_key(buffer);
    if (!client_key)
    {
        fprintf(stderr, "WebSocket 키 추출 실패\n");
        close(client_fd);
        close(server_fd);
        return -1;
    }

    accept_key = compute_accept_key(client_key);
    snprintf(response, sizeof(response),
             "HTTP/1.1 101 Switching Protocols\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Accept: %s\r\n\r\n",
             accept_key);

    send(client_fd, response, strlen(response), 0);
    free(client_key);
    free(accept_key);

    printf("WebSocket 핸드셰이크 완료. 데이터 수신 대기 중...\n");

    while ((recv_len = recv(client_fd, buffer, BUF_SIZE, 0)) > 0)
    {
        data_len = decode_ws_frame((unsigned char*)buffer, recv_len, (unsigned char*)data);
        if (data_len > 0)
        {
            printf("수신된 데이터 (%d 바이트): ", data_len);
            for (i = 0; i < data_len; i++)
                printf("%c", data[i]);
            printf("\n");
        }
    }

    close(client_fd);
    close(server_fd);
    return 0;
}

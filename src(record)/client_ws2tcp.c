/*****************************************************************************
* File       : client_ws2tcp.c
* Description: WebSocket 프레임으로 감싸서 \n 단위 레코드를 TCP 서버에 전송
*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUF_SIZE 1024
#define PORT 8331

/*****************************************************************************
* Function   : create_ws_frame_masked
* Description: WebSocket 프레임 생성 (텍스트 프레임, 마스킹 적용)
* Parameters : - const unsigned char *payload : 페이로드
*              - size_t payload_len           : 페이로드 길이
*              - size_t *frame_len            : 생성된 프레임 길이 반환 포인터
* Returns    : 프레임 포인터 (heap 메모리)
*****************************************************************************/
unsigned char* create_ws_frame_masked(const unsigned char* payload, size_t payload_len, size_t* frame_len)
{
    unsigned char *frame = NULL;
    size_t i = 0;
    size_t header_len = 2;
    size_t extra_len = 0;
    size_t mask_len = 4;
    unsigned char mask_key[4] = {0x12, 0x34, 0x56, 0x78};

    if (payload_len > 125 && payload_len < 65536)
    {
        extra_len = 2;
    }
    else if (payload_len >= 65536)
    {
        extra_len = 8;
    }

    *frame_len = header_len + extra_len + mask_len + payload_len;
    frame = malloc(*frame_len);
    if (!frame)
    {
        return NULL;
    }

    frame[0] = 0x81;  // FIN + 텍스트 프레임

    if (payload_len <= 125)
    {
        frame[1] = 0x80 | (unsigned char)payload_len;
        memcpy(frame + 2, mask_key, 4);
        for (i = 0; i < payload_len; i++)
        {
            frame[6 + i] = payload[i] ^ mask_key[i % 4];
        }
    }
    else if (payload_len < 65536)
    {
        frame[1] = 0x80 | 126;
        frame[2] = (payload_len >> 8) & 0xFF;
        frame[3] = payload_len & 0xFF;
        memcpy(frame + 4, mask_key, 4);
        for (i = 0; i < payload_len; i++)
        {
            frame[8 + i] = payload[i] ^ mask_key[i % 4];
        }
    }
    else
    {
        frame[1] = 0x80 | 127;
        for (i = 0; i < 8; i++)
        {
            frame[2 + i] = (payload_len >> ((7 - i) * 8)) & 0xFF;
        }
        memcpy(frame + 10, mask_key, 4);
        for (i = 0; i < payload_len; i++)
        {
            frame[14 + i] = payload[i] ^ mask_key[i % 4];
        }
    }

    return frame;
}

/*****************************************************************************
* Function   : main
* Description: 파일을 \n 단위로 읽어 WebSocket 프레임으로 TCP 서버에 전송
* Returns    : 0 (정상 종료), -1 (오류 발생 시)
*****************************************************************************/
int main(int argc, char *argv[])
{
    // 파일 관련
    const char *file_path = NULL;
    FILE *fp = NULL;

    // 네트워크 관련
    int sock = 0;
    struct sockaddr_in server_addr;

    // 전송 버퍼
    char line_buffer[BUF_SIZE];
    size_t line_len = 0;
    unsigned char *ws_frame = NULL;
    size_t frame_len = 0;

    // 핸드셰이크 요청/응답
    char request[512];
    char response[512];

    if (argc != 2)
    {
        fprintf(stderr, "사용법: %s <전송할 파일 경로>\n", argv[0]);
        return -1;
    }

    file_path = argv[1];
    fp = fopen(file_path, "r");
    if (!fp)
    {
        perror("파일 열기 실패");
        return -1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("소켓 생성 실패");
        fclose(fp);
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("서버 연결 실패");
        close(sock);
        fclose(fp);
        return -1;
    }

    snprintf(request, sizeof(request),
             "GET /chat HTTP/1.1\r\n"
             "Host: localhost:%d\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
             "Sec-WebSocket-Version: 13\r\n\r\n",
             PORT);

    if (send(sock, request, strlen(request), 0) < 0)
    {
        perror("핸드셰이크 요청 전송 실패");
        close(sock);
        fclose(fp);
        return -1;
    }

    if (recv(sock, response, sizeof(response) - 1, 0) <= 0)
    {
        perror("서버 응답 수신 실패");
        close(sock);
        fclose(fp);
        return -1;
    }

    printf("서버 응답: %s\n", response);
    printf("서버에 연결됨. \n 단위 레코드 전송 중...\n");

    while (fgets(line_buffer, sizeof(line_buffer), fp) != NULL)
    {
        line_len = strlen(line_buffer);
        ws_frame = create_ws_frame_masked((unsigned char *)line_buffer, line_len, &frame_len);
        if (!ws_frame)
        {
            fprintf(stderr, "WebSocket 프레임 생성 실패\n");
            break;
        }

        if (send(sock, ws_frame, frame_len, 0) < 0)
        {
            perror("프레임 전송 오류");
            free(ws_frame);
            break;
        }
        free(ws_frame);
    }

    printf("레코드 전송 완료.\n");

    close(sock);
    fclose(fp);

    return 0;
}

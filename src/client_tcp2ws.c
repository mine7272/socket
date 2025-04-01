#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>

#define BUF_SIZE 1024
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/*****************************************************************************
* Function   : do_handshake
* Description: handshake�� ����
* Returns    : 0
******************************************************************************/
int do_handshake(int sock, const char *host, const char *resource)
{
    char buffer[BUF_SIZE];
    char handshake_request[BUF_SIZE];
    const char *websocket_key = "dGhlIHNhbXBsZSBub25jZQ==";

    snprintf(handshake_request, sizeof(handshake_request),
             "GET %s HTTP/1.1\r\n"
             "Host: %s\r\n"
             "Upgrade: websocket\r\n"
             "Connection: Upgrade\r\n"
             "Sec-WebSocket-Key: %s\r\n"
             "Sec-WebSocket-Version: 13\r\n\r\n",
             resource, host, websocket_key);

    if (send(sock, handshake_request, strlen(handshake_request), 0) < 0)
    {
        perror("Handshake request ���� ����");
        return -1;
    }

    int received = recv(sock, buffer, BUF_SIZE - 1, 0);
    if (received < 0)
    {
        perror("Handshake ���� ���� ����");
        return -1;
    }

    buffer[received] = '\0';

    if (strstr(buffer, "101") == NULL)
    {
        fprintf(stderr, "Handshake ����:\n%s\n", buffer);
        return -1;
    }

    printf("Handshake ����:\n%s\n", buffer);
    return 0;
}

/*****************************************************************************
* Function   : create_ws_frame_masked
* Description: Ŭ���̾�Ʈ�� ������ ������ WebSocket �������� �����ϴ� �Լ� (����ŷ ����)
* Parameters :
* Returns    : frame
******************************************************************************/
unsigned char* create_ws_frame_masked(const unsigned char* payload, size_t payload_len, size_t* frame_len)
{
    size_t header_len;
    size_t extra_len;
    size_t mask_len;
    unsigned char *frame;
    unsigned char mask_key[4] = {0x12, 0x34, 0x56, 0x78};
    size_t i;

    header_len = 2;
    extra_len = 0;
    mask_len = 4;

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

    frame[0] = 0x80 | 0x1;

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
* Description:
* Parameters :
* Returns    :
******************************************************************************/
int main(int argc, char *argv[])
{
    const char *file_path;
    FILE *fp;
    int sock;
    struct sockaddr_in server_addr;
    unsigned char file_buffer[BUF_SIZE];
    size_t nread;
    size_t frame_len;
    unsigned char *ws_frame;

    if (argc != 2)
    {
        fprintf(stderr, "����: %s <������ ���� ���>\n", argv[0]);
        return -1;
    }

    file_path = argv[1];
    fp = fopen(file_path, "rb");
    if (!fp)
    {
        perror("���� ���� ����");
        return -1;
    }

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
    {
        perror("���� ���� ����");
        fclose(fp);
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8331);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("���� ���� ����");
        close(sock);
        fclose(fp);
        return -1;
    }

    printf("TCP ���� ���� �� WebSocket ������ �����\n");

    if (do_handshake(sock, "127.0.0.1:8331", "/") < 0)
    {
        close(sock);
        fclose(fp);
        return -1;
    }

    while ((nread = fread(file_buffer, 1, BUF_SIZE, fp)) > 0)
    {
        frame_len = 0;
        ws_frame = create_ws_frame_masked(file_buffer, nread, &frame_len);
        if (!ws_frame)
        {
            fprintf(stderr, "WebSocket ������ ���� ����\n");
            break;
        }

        if (send(sock, ws_frame, frame_len, 0) < 0)
        {
            perror("������ ���� ����");
            free(ws_frame);
            break;
        }

        free(ws_frame);
    }

    printf("���� ���� �Ϸ�.\n");

    close(sock);
    fclose(fp);
    return 0;
}


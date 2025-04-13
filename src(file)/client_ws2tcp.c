#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>

#define BUF_SIZE 1024
#define PORT 8331

unsigned char* create_ws_frame_masked(const unsigned char* payload, size_t payload_len, size_t* frame_len)
{
    unsigned char *frame;
    size_t i;
    size_t header_len = 2;
    size_t mask_len = 4;
    size_t extra_len = 0;
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

    frame[0] = 0x82;

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
* Description: ������ WebSocket ���������� ���� TCP ������ ����
* Parameters :
* Returns    :
******************************************************************************/
int main(int argc, char *argv[])
{
    const char *file_path;
    FILE *fp;
    int sock;
    struct sockaddr_in server_addr;
    unsigned char buffer[BUF_SIZE];
    size_t nread;
    unsigned char *ws_frame;
    size_t frame_len;
    char request[512];
    char response[512];

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
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("���� ���� ����");
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
        perror("�ڵ����ũ ��û ���� ����");
        close(sock);
        fclose(fp);
        return -1;
    }

    if (recv(sock, response, sizeof(response) - 1, 0) <= 0)
    {
        perror("���� ���� ���� ����");
        close(sock);
        fclose(fp);
        return -1;
    }

    printf("���� ����: %s\n", response);
    printf("������ �����. WebSocket ���������� ���� ���� ��...\n");

    while ((nread = fread(buffer, 1, BUF_SIZE, fp)) > 0)
    {
        ws_frame = create_ws_frame_masked(buffer, nread, &frame_len);
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

    printf("���� �Ϸ�.\n");

    close(sock);
    fclose(fp);

    return 0;
}
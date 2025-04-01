#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#define PORT 8331
#define BUF_SIZE 2048
#define MAX_RECV_BUF 65536

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

char* compute_accept_key(const char *client_key)
{
    const char *GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    char concatenated[256];
    unsigned char hash[SHA_DIGEST_LENGTH];

    snprintf(concatenated, sizeof(concatenated), "%s%s", client_key, GUID);
    SHA1((unsigned char*)concatenated, strlen(concatenated), hash);

    return base64_encode(hash, SHA_DIGEST_LENGTH);
}

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

int decode_ws_frame(const unsigned char *frame, size_t length, unsigned char *output, size_t *frame_len_out)
{
    size_t payload_len, offset, i;
    const unsigned char *mask_key, *payload;

    if (length < 6) return -1;

    payload_len = frame[1] & 0x7F;
    offset = 2;

    if (payload_len == 126)
    {
        if (length < 8) return -1;
        payload_len = (frame[2] << 8) | frame[3];
        offset += 2;
    }
    else if (payload_len == 127)
    {
        if (length < 14) return -1;
        payload_len = 0;
        for (i = 0; i < 8; i++)
        {
            payload_len |= ((size_t)frame[offset + i]) << (8 * (7 - i));
        }
        offset += 8;
    }

    if (length < offset + 4 + payload_len) return -1;

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

int main()
{
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    char buffer[BUF_SIZE];

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

    if (listen(server_fd, 10) < 0)
    {
        perror("listen 실패");
        close(server_fd);
        return -1;
    }

    printf("서버 실행 중 (포트 %d)...\n", PORT);

    while (1)
    {
        client_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0)
        {
            perror("accept 실패");
            continue;
        }

        printf("클라이언트 연결됨\n");

        ssize_t recv_len;
        recv_len = recv(client_fd, buffer, BUF_SIZE - 1, 0);
        if (recv_len <= 0)
        {
            perror("초기 수신 실패");
            close(client_fd);
            continue;
        }

        buffer[recv_len] = '\0';

        unsigned char *all_data = NULL;
        size_t total_len = 0;
        size_t capacity = 102400;
        all_data = malloc(capacity);
        if (!all_data)
        {
            perror("메모리 할당 실패");
            close(client_fd);
            continue;
        }

        struct timeval start, end;
        double elapsed;

        if (strncmp(buffer, "GET", 3) == 0)
        {
            char *client_key = extract_websocket_key(buffer);
            char *accept_key = compute_accept_key(client_key);
            char response[512];

            snprintf(response, sizeof(response), "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", accept_key);
            send(client_fd, response, strlen(response), 0);
            free(client_key);
            free(accept_key);

            printf("[WS] handshake 완료. 수신 시작\n");
            gettimeofday(&start, NULL);

            unsigned char recv_buf[MAX_RECV_BUF];
            size_t recv_buf_len = 0;
            char data[BUF_SIZE];

            while ((recv_len = recv(client_fd, buffer, BUF_SIZE, 0)) > 0)
            {
                if (recv_buf_len + recv_len > MAX_RECV_BUF)
                {
                    fprintf(stderr, "[WS] 누적 버퍼 초과\n");
                    break;
                }
                memcpy(recv_buf + recv_buf_len, buffer, recv_len);
                recv_buf_len += recv_len;

                size_t offset = 0;
                while (offset < recv_buf_len)
                {
                    size_t frame_len = 0;
                    int data_len = decode_ws_frame(recv_buf + offset, recv_buf_len - offset, (unsigned char*)data, &frame_len);

                    if (data_len > 0 && frame_len > 0)
                    {
                        if (total_len + data_len > capacity)
                        {
                            capacity *= 2;
                            all_data = realloc(all_data, capacity);
                            if (!all_data)
                            {
                                perror("메모리 재할당 실패");
                                break;
                            }
                        }
                        memcpy(all_data + total_len, data, data_len);
                        total_len += data_len;
                        offset += frame_len;
                    }
                    else
                    {
                        break;
                    }
                }

                if (offset > 0 && offset < recv_buf_len)
                {
                    memmove(recv_buf, recv_buf + offset, recv_buf_len - offset);
                    recv_buf_len -= offset;
                }
                else if (offset == recv_buf_len)
                {
                    recv_buf_len = 0;
                }
            }

            gettimeofday(&end, NULL);
            elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
            printf("[WS] 총 수신 바이트: %zu / 소요 시간: %.6f 초\n", total_len, elapsed);
        }
        else
        {
            gettimeofday(&start, NULL);
            memcpy(all_data, buffer, recv_len);
            total_len = recv_len;

            while ((recv_len = recv(client_fd, buffer, BUF_SIZE, 0)) > 0)
            {
                if (total_len + recv_len > capacity)
                {
                    capacity *= 2;
                    all_data = realloc(all_data, capacity);
                    if (!all_data)
                    {
                        perror("메모리 재할당 실패");
                        break;
                    }
                }
                memcpy(all_data + total_len, buffer, recv_len);
                total_len += recv_len;
            }

            gettimeofday(&end, NULL);
            elapsed = (end.tv_sec - start.tv_sec) + (end.tv_usec - start.tv_usec) / 1000000.0;
            printf("[TCP] 총 수신 바이트: %zu / 소요 시간: %.6f 초\n", total_len, elapsed);
        }

        free(all_data);
        close(client_fd);
        printf("클라이언트 연결 종료\n\n");
    }

    close(server_fd);
    return 0;
}

/*****************************************************************************
* File       : server_tcpws.c
* Description: TCP �� WebSocket ���������� ó���ϴ� ���� ���α׷� (select ��� ��Ƽ�÷���)
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
* Description: Ŭ���̾�Ʈ ���Ằ ������ ����
*****************************************************************************/
struct client_data
{
    int fd;                             // Ŭ���̾�Ʈ ���� ���� ��ũ����
    int is_websocket;                   // WebSocket ���� ����
    unsigned char recv_buf[MAX_RECV_BUF]; // ���� ����
    size_t recv_buf_len;                // ���� ���ۿ� ����� ������ ����
    unsigned char *all_data;            // ��ü ���� ������
    size_t total_len;                   // ��ü ���� ������ ����
    size_t capacity;                    // �Ҵ�� ���� ũ��
    size_t record_count;                // ������ ���ڵ� �� (�� �ٲ� ����)
    struct timeval start_time;          // ���� ���� �ð�
    int handshake_completed;            // WebSocket �ڵ����ũ �Ϸ� ����
};

/*****************************************************************************
* Function   : base64_encode
* Description: ���̳ʸ� �����͸� base64�� ���ڵ�
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
* Description: WebSocket �ڵ����ũ�� Accept Ű ����
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
* Description: ��û ������� Sec-WebSocket-Key ����
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
* Description: WebSocket �������� ���ڵ��ϰ� ����ŷ ���� (�ҿ��� ������ ��� ó�� ����)
*****************************************************************************/
int decode_ws_frame(const unsigned char *frame, size_t length, unsigned char *output, size_t *frame_len_out)
{
    size_t payload_len = 0;
    size_t offset = 0;
    size_t i = 0;
    const unsigned char *mask_key = NULL;
    const unsigned char *payload = NULL;

    if (length < 6)
        return 0; // ������ ������� ������ �� ���

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
        return 0; // �����Ͱ� ���� ������ ���ŵ��� ���� �� ���

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
* Description: �� Ŭ���̾�Ʈ ���� ó��
*****************************************************************************/
int handle_new_connection(int server_fd, fd_set *master_set, int *max_fd, struct client_data *clients)
{
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
    int i;
    
    if (client_fd < 0)
    {
        perror("accept ����");
        return -1;
    }
    
    printf("Ŭ���̾�Ʈ �����\n");
    
    // �� ���� ã��
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
                perror("�޸� �Ҵ� ����");
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
    
    printf("�ִ� Ŭ���̾�Ʈ ���� �� �ʰ�\n");
    close(client_fd);
    return -1;
}

/*****************************************************************************
* Function   : handle_websocket_data
* Description: WebSocket ������ ó��
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
        fprintf(stderr, "���� �ʰ�. ���� �ߴ�\n");
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
                    fprintf(stderr, "�޸� ���Ҵ� ����\n");
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
            fprintf(stderr, "������ ���ڵ� ���� %d %zu\n", data_len, frame_len);
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
* Description: �Ϲ� TCP ������ ó��
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
            fprintf(stderr, "�޸� ���Ҵ� ����\n");
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
* Description: Ŭ���̾�Ʈ ������ ���� �� ó��
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
            // ���� ����
            gettimeofday(&end_time, NULL);
            diff = (end_time.tv_sec - client->start_time.tv_sec) + 
                   (end_time.tv_usec - client->start_time.tv_usec) / 1000000.0;
            
            if (client->is_websocket)
            {
                printf("[WS] �� ���� ����Ʈ: %zu, ���ڵ� ��: %zu, �ҿ� �ð�: %.6f ��\n", 
                       client->total_len, client->record_count, diff);
            }
            else
            {
                printf("[TCP] �� ���� ����Ʈ: %zu, ���ڵ� ��: %zu, �ҿ� �ð�: %.6f ��\n", 
                       client->total_len, client->record_count, diff);
            }
            
            printf("Ŭ���̾�Ʈ ���� ����\n\n");
        }
        else
        {
            perror("recv ����");
        }
        
        free(client->all_data);
        close(client->fd);
        FD_CLR(client->fd, master_set);
        client->fd = -1;
        return;
    }
    
    buffer[recv_len] = '\0';
    
    // �ʱ� ���� Ȯ�� (WebSocket �ڵ����ũ ���� �Ǵ�)
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
            printf("[WS] handshake �Ϸ�. ���� ����\n");
            gettimeofday(&client->start_time, NULL);
        }
        else
        {
            fprintf(stderr, "WebSocket Ű ���� ����\n");
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
* Description: TCP �� WebSocket ���� ���� ��ƾ
*****************************************************************************/
int main()
{
    int server_fd = 0;
    struct sockaddr_in server_addr;
    int i, select_result, fd;
    
    fd_set master_set, working_set;
    int max_fd;
    struct client_data clients[MAX_CLIENTS];
    
    // Ŭ���̾�Ʈ �迭 �ʱ�ȭ
    for (i = 0; i < MAX_CLIENTS; i++)
    {
        clients[i].fd = -1;
        clients[i].all_data = NULL;
    }
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("���� ���� ����");
        return -1;
    }
    
    // SO_REUSEADDR �ɼ� ����
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt ����");
        close(server_fd);
        return -1;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind ����");
        close(server_fd);
        return -1;
    }
    
    if (listen(server_fd, 10) < 0)
    {
        perror("listen ����");
        close(server_fd);
        return -1;
    }
    
    printf("���� ���� �� (��Ʈ %d)...\n", PORT);
    
    // select �ʱ�ȭ
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
            perror("select ����");
            break;
        }
        
        if (select_result == 0)
        {
            // Ÿ�Ӿƿ�, �ʿ�� �߰� �۾� ����
            continue;
        }
        
        for (fd = 0; fd <= max_fd; fd++)
        {
            if (FD_ISSET(fd, &working_set))
            {
                if (fd == server_fd)
                {
                    // �� ���� ��û
                    handle_new_connection(server_fd, &master_set, &max_fd, clients);
                }
                else
                {
                    // Ŭ���̾�Ʈ ������ ó��
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
    
    // ����
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

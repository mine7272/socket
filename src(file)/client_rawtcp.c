#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUF_SIZE 1024
#define PORT 8331

/*****************************************************************************
* Function   : main
* Description: �Ϲ� TCP ������ ����Ͽ� ������ ������ ����
*****************************************************************************/
int main(int argc, char *argv[])
{
    const char *file_path;
    FILE *fp;
    int sock;
    struct sockaddr_in server_addr;
    unsigned char buffer[BUF_SIZE];
    size_t nread;

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

    printf("������ �����. ���� ���� ����...\n");

    while ((nread = fread(buffer, 1, BUF_SIZE, fp)) > 0)
    {
        if (send(sock, buffer, nread, 0) < 0)
        {
            perror("������ ���� ����");
            break;
        }
    }

    printf("���� ���� �Ϸ�.\n");

    close(sock);
    fclose(fp);

    return 0;
}


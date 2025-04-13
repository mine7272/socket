/*****************************************************************************
* File       : client_rawtcp.c
* Description: �Ϲ� TCP ������ ����Ͽ� \n ���� ���ڵ� ������ ������ ����
*****************************************************************************/

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
* Description: \n ���� ���ڵ� ������ TCP ������ ���� ����
* Parameters : - int argc       : ���� ����
*              - char *argv[]   : ���� �迭
* Returns    : 0 (���� ����), -1 (���� �߻� ��)
*****************************************************************************/
int main(int argc, char *argv[])
{
    // ���� ����
    const char *file_path = NULL;
    FILE *fp = NULL;

    // ��Ʈ��ũ ����
    int sock = 0;
    struct sockaddr_in server_addr;

    // ���� �� ó��
    char line_buffer[BUF_SIZE];
    size_t len = 0;
    ssize_t sent_len = 0;

    // ���� ó��
    if (argc != 2)
    {
        fprintf(stderr, "����: %s <������ ���� ���>\n", argv[0]);
        return -1;
    }

    file_path = argv[1];
    fp = fopen(file_path, "r");
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

    printf("������ �����. ���ڵ� ���� ���� ����...\n");

    while (fgets(line_buffer, sizeof(line_buffer), fp) != NULL)
    {
        len = strlen(line_buffer);
        sent_len = send(sock, line_buffer, len, 0);
        if (sent_len < 0)
        {
            perror("������ ���� ����");
            break;
        }
    }

    printf("��� ���ڵ� ���� �Ϸ�.\n");

    close(sock);
    fclose(fp);

    return 0;
}

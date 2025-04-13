/*****************************************************************************
* File       : client_rawtcp.c
* Description: 일반 TCP 소켓을 사용하여 \n 기준 레코드 단위로 파일을 전송
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
* Description: \n 기준 레코드 단위로 TCP 서버에 파일 전송
* Parameters : - int argc       : 인자 개수
*              - char *argv[]   : 인자 배열
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

    // 버퍼 및 처리
    char line_buffer[BUF_SIZE];
    size_t len = 0;
    ssize_t sent_len = 0;

    // 예외 처리
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

    printf("서버에 연결됨. 레코드 단위 전송 시작...\n");

    while (fgets(line_buffer, sizeof(line_buffer), fp) != NULL)
    {
        len = strlen(line_buffer);
        sent_len = send(sock, line_buffer, len, 0);
        if (sent_len < 0)
        {
            perror("데이터 전송 오류");
            break;
        }
    }

    printf("모든 레코드 전송 완료.\n");

    close(sock);
    fclose(fp);

    return 0;
}

# 파일 전송 실습: TCP & WebSocket 조합별 클라이언트/서버

## 개요

이 프로젝트는 총 6가지 통신 조합을 통해 **파일 전송 기능**을 실습합니다.  
WebSocket은 핸드셰이크 및 프레임 마스킹을 직접 구현했으며, 성능 비교를 위해 **수신 시간 측정** 기능도 포함되어 있습니다.

---

## 전체 통신 조합 정리

| 클라이언트 프로그램   | 서버 프로그램     | 설명                                                                  |
|------------------|------------------|---------------------------------------------------------------------|
| client_rawtcp.c  | (X) server_tcpws.c | [주의] WebSocket 핸드셰이크 없이 순수 TCP 전송 → 서버와 통신 불가             |
| client_tcp2ws.c  | server_tcpws.c   | TCP 연결 후 WebSocket 핸드셰이크 + 프레임 마스킹 적용                          |
| client_ws2tcp.c  | server_tcpws.c   | WebSocket 프레임만 전송 (가짜 WS 클라이언트) → 서버에서 프레임 디코딩 필요       |
| client_ws.c      | server_ws.c      | 표준 WebSocket 라이브러리 기반 클라이언트                                 |
| client_ws.c      | server_tcpws.c   | 라이브러리 기반 클라이언트 → 직접 구현된 WebSocket 서버                        |
| client_tcp2ws.c  | server_ws.c      | 수동 구현 클라이언트 → 라이브러리 기반 WebSocket 서버                         |

---

## client_ws2tcp.c vs client_tcp2ws.c 차이점

| 항목                     | client_ws2tcp.c                            | client_tcp2ws.c                            |
|------------------------|-------------------------------------------|-------------------------------------------|
| 핸드셰이크 수행 여부          | X 수행하지 않음 (단순 TCP 연결)             | O WebSocket 핸드셰이크 수행                  |
| WebSocket 프레임 마스킹     | O 직접 구현                                | O 직접 구현                                |
| 서버 요구 조건              | 서버가 WebSocket 프레임만 처리 가능해야 함         | 서버가 WebSocket 핸드셰이크 및 프레임 모두 처리 가능해야 함 |
| 통신 대상 서버             | server_tcpws.c                             | server_tcpws.c, server_ws.c               |
| 용도                     | WebSocket처럼 보이지만 실제 WS가 아닌 상황        | 실제 WebSocket 통신 구조 그대로 재현          |

---

## 실행 방법 예시

```bash
make
./server_tcpws
./server_ws

./client_rawtcp sample.txt
./client_tcp2ws sample.txt
./client_ws2tcp sample.txt
./client_ws sample.txt
```

---

## 실행 결과 예시

```bash
# server_tcpws 실행 시
WebSocket 서버 실행 중 (포트 8331)...
HTTP 요청: ...
WebSocket 핸드셰이크 완료. 데이터 수신 대기 중...
파일 수신 완료. 소요 시간: 0.108932 초

# client_tcp2ws 실행 시
TCP 연결 성공 → WebSocket 서버에 연결됨
Handshake 성공: HTTP/1.1 101 Switching Protocols ...
파일 전송 완료

# client_ws 실행 시
CLIENT: 연결 성립됨
CLIENT: 파일 전송 완료, 프로그램 종료

# server_ws 실행 시
웹소켓 파일 수신 서버가 포트 8331에서 시작됨.
클라이언트 연결됨.
파일 전송 완료: 총 14XXX 바이트 수신, 소요 시간: 0.112000 초
```

---

## 에러 사례 및 해결 방안 정리

### 1. WebSocket 핸드셰이크 실패
- **현상**: `101 Switching Protocols` 실패
- **원인**: 헤더 누락 (`Sec-WebSocket-Key`) 또는 `Accept-Key` 계산 오류
- **해결**: 클라이언트 헤더 체크, 서버의 SHA1 + Base64 로직 검증

### 2. WebSocket 프레임 디코딩 실패
- **원인**: offset 계산 누락, 마스킹 키 적용 안됨
- **해결**: payload 길이, 마스킹 키 해석 정확히 구현

### 3. client_rawtcp → server_tcpws 통신 실패
- **현상**: `WebSocket 키 추출 실패` 메시지
- **해결**: WebSocket이 아닌 순수 TCP 전송 → 별도 서버 필요


---

## 참고

- 테스트 포트: `127.0.0.1:8331`
- WebSocket 프레임: RFC 6455 기반 수동 처리
- WebSocket 서버는 libwebsockets 사용 (`server_ws.c`)
- 모든 빌드는 `make` 명령어 사용

---

## 작성자

김민회 (mine7272)

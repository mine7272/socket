# 파일 전송 실습: TCP & WebSocket 조합별 클라이언트/서버 (v2)

## 개요

이 프로젝트는 총 6가지 통신 조합을 통해 **파일 전송 기능**을 실습합니다.  
WebSocket은 핸드셰이크 및 프레임 마스킹을 직접 구현했으며, 성능 비교를 위해 **수신 시간 측정** 기능도 포함되어 있습니다.

이번 v2에서는 **WebSocket 프레임 누적 처리**, **서버 지속 수신 처리**, **디코딩 오류 해결**, **대용량 안정성 개선**,  
그리고 `client_rawtcp`와의 **순수 TCP 통신 지원**까지 포함되어 있습니다.

---

## 전체 통신 조합 정리

| 클라이언트 프로그램   | 서버 프로그램     | 설명                                                                  |
|------------------|------------------|---------------------------------------------------------------------|
| client_rawtcp.c  | server_tcpws.c   | WebSocket이 아닌 순수 TCP 전송                |
| client_tcp2ws.c  | server_tcpws.c   | TCP 연결 후 WebSocket 핸드셰이크 + 프레임 마스킹 적용                          |
| client_ws2tcp.c  | server_tcpws.c   | WebSocket 프레임만 전송 (가짜 WS 클라이언트) → 서버에서 프레임 디코딩 필요       |
| client_ws.c      | server_tcpws.c   | **라이브러리** 기반 클라이언트 → 직접 구현된 WebSocket 서버                    |
| client_ws.c      | server_ws.c      | 표준 WebSocket **라이브러리 기반** 클라이언트                                 |
| client_tcp2ws.c  | server_ws.c      | 수동 구현 클라이언트 → **라이브러리 기반** WebSocket 서버                         |

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
./server_tcpws
./server_ws

./client_rawtcp [파일이름]
./client_tcp2ws [파일이름]
./client_ws2tcp [파일이름]
./client_ws [파일이름]
```

---

## 실행 결과 예시 (텍스트 버전)

```
서버 실행 중 (포트 8331)...
클라이언트 연결됨
[WS] handshake 완료. 수신 시작
[WS] 총 수신 바이트: 1017558069 / 소요 시간: 1.446383 초
클라이언트 연결 종료

클라이언트 연결됨
[TCP] 총 수신 바이트: 1017558069 / 소요 시간: 0.408939 초
클라이언트 연결 종료

클라이언트 연결됨
[WS] handshake 완료. 수신 시작
[WS] 총 수신 바이트: 1017558069 / 소요 시간: 1.544712 초
클라이언트 연결 종료

클라이언트 연결됨
[WS] handshake 완료. 수신 시작
[WS] 총 수신 바이트: 1017558069 / 소요 시간: 4.624147 초
클라이언트 연결 종료
```

---

## 📌 서버 프로그램 특징 (server_tcpws.c 기준)

- WebSocket 클라이언트와 TCP 클라이언트를 모두 수신 가능
- WebSocket 핸드셰이크 직접 구현 (`Sec-WebSocket-Key` → SHA1 + Base64 → Accept Key)
- WebSocket 프레임 직접 해석 및 마스킹 해제 처리
- `recv()`에서 잘린 프레임도 처리 가능 (누적 버퍼 + 오프셋 기반 파싱)
- `malloc` + `realloc` 기반으로 대용량 파일 수신 가능
- 클라이언트 연결 종료 후에도 서버는 계속 수신 대기 (`while(1)`)

---

## 🚨 에러 사례 및 해결 방안

### 1. WebSocket 프레임 디코딩 실패 (`[WS] 프레임 디코딩 실패 또는 잘림`)
- **원인**: recv()에서 프레임이 분할되어 도착함
- **해결**: 누적 버퍼 및 offset 기준 파싱 방식 적용 (v2 반영)

### 2. Segmentation fault
- **원인**: 고정 버퍼(`char all_data[102400]`)로는 수 GB 수신 시 오버플로우 발생
- **해결**: `malloc + realloc` 방식으로 수정

### 3. WebSocket 핸드셰이크 실패
- **현상**: `101 Switching Protocols` 실패
- **원인**: 헤더 누락 (`Sec-WebSocket-Key`) 또는 `Accept-Key` 계산 오류
- **해결**: 클라이언트 헤더 체크, 서버의 SHA1 + Base64 로직 검증

### 4. client_rawtcp → server_tcpws 통신 실패 (과거)
- **현상**: WebSocket 키 추출 실패 → 수신 불가
- **해결**: WebSocket이 아닌 순수 TCP로 인식되어 이제 별도 분기 처리 (v2 적용)

---

## 참고

- 테스트 포트: `127.0.0.1:8331`
- WebSocket 프레임: RFC 6455 기반 수동 처리
- WebSocket 서버는 libwebsockets 사용 (`server_ws.c`)
- 모든 빌드는 `make` 명령어 사용

---

## 작성자

김민회 (mine7272)  
업데이트: 2025-04-01 / 버전: v2

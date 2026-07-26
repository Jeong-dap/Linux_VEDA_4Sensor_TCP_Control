# ── 컴파일러 설정 ──────────────────────────────────────────
# 우분투에서 make all 시 server/lib은 크로스컴파일, client는 네이티브 빌드
# 라즈베리파이에서 직접 빌드할 경우: make CROSS= all
PIHOST   = njd990603@172.20.33.119
PIDIR    = /home/njd990603/Project

CROSS    = aarch64-linux-gnu-
CC_SRV   = $(CROSS)gcc           # 서버·라이브러리용 (ARM64)
CC_CLI   = gcc                   # 클라이언트용 (x86_64)

CFLAGS   = -std=gnu11 -Wall -Wextra -Wformat=2 -I include
LDFLAGS  = -lpthread -ldl
HOST_CC ?= gcc

# WiringPi 크로스 컴파일 환경 경로 (필요 시 수정)
# 라즈베리파이에서 직접 빌드 시에는 자동으로 /usr에서 찾음
WIRING_INC ?=
WIRING_LIB ?=

SRV_CFLAGS  = $(CFLAGS) $(if $(WIRING_INC),-I$(WIRING_INC))
SRV_LDFLAGS = $(if $(WIRING_LIB),-L$(WIRING_LIB)) -lwiringPi -lpthread

.PHONY: all server client lib test test-c test-build test-web analyze clean deploy

all: lib server client

lib:
	$(CC_SRV) -fPIC -shared $(SRV_CFLAGS) -o lib/led.so          lib/led.c          $(SRV_LDFLAGS)
	$(CC_SRV) -fPIC -shared $(SRV_CFLAGS) -o lib/buzzer.so        lib/buzzer.c       $(SRV_LDFLAGS)
	$(CC_SRV) -fPIC -shared $(SRV_CFLAGS) -o lib/cds.so            lib/cds.c          $(SRV_LDFLAGS)
	$(CC_SRV) -fPIC -shared $(SRV_CFLAGS) -o lib/segment.so       lib/segment.c      $(SRV_LDFLAGS)

server: lib
	$(CC_SRV) $(SRV_CFLAGS) -o alarm_server server/main.c server/daemon.c server/handler.c common/protocol_io.c \
	    -Wl,-rpath,$(abspath lib) $(LDFLAGS) $(SRV_LDFLAGS)

client:
	$(CC_CLI) $(CFLAGS) -o alarm_client client/main.c common/protocol_io.c $(LDFLAGS)

test: test-c test-build test-web

test-c:
	$(HOST_CC) $(CFLAGS) -o tests/test_protocol_io tests/test_protocol_io.c common/protocol_io.c -lpthread
	./tests/test_protocol_io

test-build:
	$(HOST_CC) $(CFLAGS) -I tests/stubs -fsyntax-only \
	    client/main.c common/protocol_io.c lib/*.c server/*.c

test-web:
	cd web && node --test

analyze:
	cppcheck --enable=warning,performance,portability --std=c11 \
	    --suppress=missingIncludeSystem -I include client common lib server tests

deploy:
	-ssh $(PIHOST) pkill -f alarm_server
	ssh $(PIHOST) mkdir -p $(PIDIR)/lib $(PIDIR)/web
	scp alarm_server $(PIHOST):$(PIDIR)/
	scp lib/*.so     $(PIHOST):$(PIDIR)/lib/
	scp web/server.js web/protocol.js web/index.html web/package.json web/package-lock.json \
	    $(PIHOST):$(PIDIR)/web/

clean:
	rm -f alarm_server alarm_client lib/*.so tests/test_protocol_io

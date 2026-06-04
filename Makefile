# ── 컴파일러 설정 ──────────────────────────────────────────
# 우분투에서 make all 시 server/lib은 크로스컴파일, client는 네이티브 빌드
# 라즈베리파이에서 직접 빌드할 경우: make CROSS= all
PIHOST   = njd990603@172.20.33.119
PIDIR    = /home/njd990603/Project

CROSS    = aarch64-linux-gnu-
CC_SRV   = $(CROSS)gcc           # 서버·라이브러리용 (ARM64)
CC_CLI   = gcc                   # 클라이언트용 (x86_64)

CFLAGS   = -Wall -I include
LDFLAGS  = -lpthread -ldl

# WiringPi 크로스 컴파일 환경 경로 (필요 시 수정)
# 라즈베리파이에서 직접 빌드 시에는 자동으로 /usr에서 찾음
WIRING_INC ?=
WIRING_LIB ?=

SRV_CFLAGS  = $(CFLAGS) $(if $(WIRING_INC),-I$(WIRING_INC))
SRV_LDFLAGS = -lwiringPi $(if $(WIRING_LIB),-L$(WIRING_LIB))

.PHONY: all server client lib clean deploy

all: lib server client

lib:
	$(CC_SRV) -fPIC -shared $(SRV_CFLAGS) -o lib/led.so          lib/led.c          $(SRV_LDFLAGS)
	$(CC_SRV) -fPIC -shared $(SRV_CFLAGS) -o lib/buzzer.so        lib/buzzer.c       $(SRV_LDFLAGS)
	$(CC_SRV) -fPIC -shared $(SRV_CFLAGS) -o lib/cds.so            lib/cds.c          $(SRV_LDFLAGS)
	$(CC_SRV) -fPIC -shared $(SRV_CFLAGS) -o lib/segment.so       lib/segment.c      $(SRV_LDFLAGS)

server: lib
	$(CC_SRV) $(SRV_CFLAGS) -o alarm_server server/main.c server/daemon.c server/handler.c \
	    -Wl,-rpath,$(abspath lib) $(LDFLAGS) $(SRV_LDFLAGS)

client:
	$(CC_CLI) $(CFLAGS) -o alarm_client client/main.c $(LDFLAGS)

deploy:
	-ssh $(PIHOST) pkill -f alarm_server
	ssh $(PIHOST) mkdir -p $(PIDIR)/lib $(PIDIR)/web
	scp alarm_server $(PIHOST):$(PIDIR)/
	scp lib/*.so     $(PIHOST):$(PIDIR)/lib/
	scp web/server.js web/index.html web/package.json $(PIHOST):$(PIDIR)/web/

clean:
	rm -f alarm_server alarm_client lib/*.so

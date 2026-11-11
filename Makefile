CC ?= gcc
CFLAGS = -std=c11 -O2 -Wall -Wextra -pthread
ifeq ($(OS),Windows_NT)
  EXE = .exe
  CFLAGS += -D_WIN32_WINNT=0x0601 -IC:/Program\ Files/mingw64/opt/include
  LDLIBS = -LC:/Program\ Files/mingw64/opt/lib -lsqlite3 -lws2_32 -lbcrypt -pthread
else
  EXE =
  LDLIBS = -lsqlite3 -lcrypto -pthread
endif

all: server$(EXE) client$(EXE)
server$(EXE): server.c net.c net.h password.c password.h seed_questions.c seed_questions.h
	$(CC) $(CFLAGS) server.c net.c password.c seed_questions.c -o $@ $(LDLIBS)
client$(EXE): client.c net.c net.h
	$(CC) $(CFLAGS) client.c net.c -o $@ $(LDLIBS)
clean:
	$(RM) server$(EXE) client$(EXE)
.PHONY: all clean

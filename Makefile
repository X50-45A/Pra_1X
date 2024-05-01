CC = gcc
CFLAGS = -std=c99 -pthread
SERVER_SRC = servidor.c
CLIENT1_PY = client.py
CLIENT2_PY = client.py
SERVER_BIN = server
CLIENT1_BIN = client1
CLIENT2_BIN = client2

all: $(SERVER_BIN)
	$(CC) $(CFLAGS) -o $(SERVER_BIN) $(SERVER_SRC)

	 "python3 $(CLIENT1_PY) -d -c client1.cfg" &
	 "python3 $(CLIENT2_PY) -d -c client2.cfg" &

clean:
	rm -f $(SERVER_BIN) $(CLIENT1_BIN) $(CLIENT2_BIN)

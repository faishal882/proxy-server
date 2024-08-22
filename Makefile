CC=g++
CFlags= -g -Wall

all: proxy

	
proxy: proxy_server_with_cache.c
	$(CC) $(CFlags) -o parse_request.o -c parse_request.c -lpthread
	$(CC) $(CFlags) -o proxy.o -c proxy_server_with_cache.c -lpthread
	$(CC) $(CFlags) -o proxy parse_request.o proxy.o -lpthread

clean:
	rm -f  *.o

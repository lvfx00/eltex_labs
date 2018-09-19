all:
	cc server.c sem_util.c -o server
	cc client.c sem_util.c -pthread -lncurses -o client
clean:
	rm -rf *.o client server

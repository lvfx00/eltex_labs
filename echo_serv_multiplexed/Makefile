make:
	cc serv_poll.c serv_util.c sock_util.c -o serv_poll.out
	cc serv_select.c serv_util.c sock_util.c -o serv_select.out
	cc serv_epoll.c serv_util.c sock_util.c -o serv_epoll.out

clean:
	rm -rf *.o *.out


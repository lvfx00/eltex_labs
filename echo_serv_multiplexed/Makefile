make:
	cc serv_poll.c serv_util.c sock_util.c -o serv_poll
	cc serv_select.c serv_util.c sock_util.c -o serv_select
	cc serv_epoll.c serv_util.c sock_util.c -o serv_epoll

clean:
	rm -rf *.o serv_epoll serv_poll serv_select


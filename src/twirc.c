#include <stdio.h>	// NULL, fprintf(), perror()
#include <stdlib.h>	// NULL, EXIT_FAILURE, EXIT_SUCCESS
#include <errno.h>	// errno
#include <netdb.h>	// getaddrinfo()
#include <unistd.h>	// close(), fcntl()
#include <string.h>	// strerror()
#include <fcntl.h>	// fcntl()
#include <sys/types.h>	// ssize_t
#include <sys/socket.h> // socket(), connect(), send(), recv()
#include <sys/epoll.h>  // epoll_create(), epoll_ctl(), epoll_wait()
#include "stcpnb.c"

#define TWIRC_NAME "twirc"
#define TWIRC_VER_MAJOR 0
#define TWIRC_VER_MINOR 1
#ifdef BUILD
	#define TWIRC_VER_BUILD BUILD
#else
	#define TWIRC_VER_BUILD 0.0
#endif

#define TWIRC_IPV4 AF_INET
#define TWIRC_IPV6 AF_INET6

#define TWIRC_STATUS_DISCONNECTED 0
#define TWIRC_STATUS_CONNECTING   1
#define TWIRC_STATUS_CONNECTED    2

#define TWIRC_BUFFER_SIZE 1024

struct twirc_state
{
	int status;
	int ip_type;
	int socket_fd;
};

int twirc_sock_crt(struct twirc_state *state)
{
	int sockfd = stcpnb_create(state->ip_type);
	if (sockfd < 0)
	{
		// Socket could not be created
		return 0;
	}

	// All worked out, let's set the appropriate fields
	state->socket_fd = sockfd;
	state->status = TWIRC_STATUS_DISCONNECTED;
	return 1;
}

int twirc_sock_con(struct twirc_state *state, const char *host, const char *port)
{
	int con = stcpnb_connect(state->socket_fd, state->ip_type, host, port);

	if (con < 0)
	{
		// Socket could not be connected
		return 0;
	}

	state->status = TWIRC_STATUS_CONNECTING;
	return 1;
}

int twirc_sock_snd(struct twirc_state *state, const char *msg, size_t len)
{
	fprintf(stderr, "twirc_sock_snd()\n");
	//stcpnb_send(state->socket_fd, msg, len);
	return 0;
}

int twirc_sock_rcv(struct twirc_state *state /*, char *buf, size_t len */)
{
	fprintf(stderr, "twitch_sock_rcv()\n");

	ssize_t res_len;
	char buf[1024];
	res_len = recv(state->socket_fd, buf, 1024, MSG_TRUNC);
	buf[res_len] = '\0';

	fprintf(stdout, "MSG: %s\n", buf);

	if (res_len >= 1024)
	{
		return 0;
	}
	return 1;
}

/*
 * main
 */
int main(void)
{
	fprintf(stderr, "Starting up %s version %o.%o build %f\n",
		TWIRC_NAME, TWIRC_VER_MAJOR, TWIRC_VER_MINOR, TWIRC_VER_BUILD);

	struct twirc_state s;
	s.ip_type = AF_INET; // IPv4

	if (twirc_sock_crt(&s) == 0)
	{
		perror("Could not create socket");
		return EXIT_FAILURE;
	}

	fprintf(stderr, "Socket created...\n");

	int epfd = epoll_create(1);
	if (epfd < 0)
	{
		perror("Could not create epoll file descriptor");
		return EXIT_FAILURE;
	}

	struct epoll_event eev = { 0 };
	eev.data.ptr = &s;
	eev.events = EPOLLRDHUP | EPOLLOUT | EPOLLIN | EPOLLET;
	int epctl_result = epoll_ctl(epfd, EPOLL_CTL_ADD, s.socket_fd, &eev);
	
	if (epctl_result)
	{
		perror("Socket could not be registered for IO");
		return EXIT_FAILURE;
	}

	fprintf(stderr, "Socket registered for IO...\n");

	if (twirc_sock_con(&s, "irc.chat.twitch.tv", "6667") == 0)
	{
		if (errno = EINPROGRESS)
		{
			fprintf(stderr, "Connection initiated...\n");
		}
		else
		{
			perror("Could not connect socket");
			return EXIT_FAILURE;
		}
	}

	struct epoll_event epev;

	int running = 1;
	while (running)
	{
		int num_events = epoll_wait(epfd, &epev, 1, 1 * 1000);

		if (num_events == -1)
		{
			perror("epoll_wait encountered an error");
		}

		if (epev.events & EPOLLIN)
		{
			struct twirc_state *state = ((struct twirc_state*) epev.data.ptr);
			//socket_receive(state);
			twirc_sock_rcv(state);
		}

		if (epev.events & EPOLLOUT)
		{
			struct twirc_state *state = ((struct twirc_state*) epev.data.ptr);
			if (state->status == TWIRC_STATUS_CONNECTING)
			{
				//check_connection(state);
				stcpnb_status(state->socket_fd);
			}
			//socket_send(state);
			twirc_sock_snd(state, "PING", 5);
		}

		if (epev.events & EPOLLRDHUP)
		{
			fprintf(stderr, "EPOLLRDHUP (peer closed socket connection)\n");
		}

		if (epev.events & EPOLLHUP) // will fire, even if not added explicitly
		{
			fprintf(stderr, "EPOLLHUP (peer closed channel)\n");
		}

	}

	close(epfd);
	return EXIT_SUCCESS;
}


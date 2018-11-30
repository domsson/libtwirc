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

/*
struct twirc_creds
{
	char *host;
	unsigned short port;
	char *nick;
	char *pass;
};
*/

int twirc_connect(struct twirc_state *state, const char *host, const char *port)
{
	// Returns  0 if connection is in progress
	// Returns -1 if connection attempt failed (check errno!)
	// Returns -2 if host/port could not be resolved to IP
	int con = stcpnb_connect(state->socket_fd, state->ip_type, host, port);

	if (con < 0)
	{
		// Socket could not be connected
		fprintf(stderr, "Socket connection failed with error code %d\n", con);
		return 0;
	}

	state->status = TWIRC_STATUS_CONNECTING;
	return 1;
}

int twirc_send(struct twirc_state *state, const char *msg, size_t len)
{
	// Get the actual message length (without null terminator)
	// If the message is too big for the message buffer, we only
	// grab as much as we can fit in our buffer (we truncate)
	size_t msg_len = strnlen(msg, TWIRC_BUFFER_SIZE - 3);

	// Create a perfectly sized buffer (max TWIRC_BUFFER_SIZE)
	size_t buf_len = msg_len + 3;
	char *buf = malloc(buf_len * sizeof(char));

	// Copy the user's message into our slightly larger buffer
	snprintf(buf, buf_len, "%s", msg);

	// Use the additional space for line and null terminators
	// IRC messages need to be CR-LF (\r\n) terminated!
	buf[msg_len+0] = '\r';
	buf[msg_len+1] = '\n';
	buf[msg_len+2] = '\0';

	fprintf(stderr, "twirc_send (%d): %s\n", strlen(buf), buf);
	stcpnb_send(state->socket_fd, buf, buf_len);
	free(buf);

	return 1;
}

int twirc_recv(struct twirc_state *state /*, char *buf, size_t len */)
{
	ssize_t res_len;
	char buf[1024];
	res_len = recv(state->socket_fd, buf, 1024, MSG_TRUNC);
	buf[res_len] = '\0';

	fprintf(stdout, "twirc_recv (%d): %s\n", res_len, buf);

	if (res_len == 0)
	{
		// TODO socket disconnected!
		return 0;
	}

	if (res_len == -1)
	{
		// TODO error!
		perror("recv() error");
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			fprintf(stderr, "(EAGAIN or EWOULDBLOCK)\n");
		}
		if (errno == EBADF)
		{
			fprintf(stderr, "(Invalid socket)\n");
		}
		if (errno == ECONNREFUSED)
		{
			fprintf(stderr, "(Connection refused)\n");
		}
		if (errno == EFAULT)
		{
			fprintf(stderr, "(Buffer error)\n");
		}
		if (errno == ENOTCONN)
		{
			fprintf(stderr, "(Socket not connected)\n");
		}
		return 0;
	}

	if (res_len >= 1024)
	{
		fprintf(stderr, "twirc_recv: (message truncated)");
		return 0;
	}
	return 1;
}

/* how do we best pass in the credentials? just like this or in a struct? */
int twirc_auth(struct twirc_state *state, const char *nick, const char *pass)
{
	char msg_pass[TWIRC_BUFFER_SIZE];
	snprintf(msg_pass, TWIRC_BUFFER_SIZE, "PASS %s", pass);

	char msg_nick[TWIRC_BUFFER_SIZE];
	snprintf(msg_nick, TWIRC_BUFFER_SIZE, "NICK %s", nick);

	twirc_send(state, msg_pass, TWIRC_BUFFER_SIZE);
	twirc_send(state, msg_nick, TWIRC_BUFFER_SIZE);
}

int twirc_cmd_quit(struct twirc_state *state)
{
	char msg[TWIRC_BUFFER_SIZE];
	snprintf(msg, TWIRC_BUFFER_SIZE, "QUIT");
	twirc_send(state, msg, TWIRC_BUFFER_SIZE);
	return 1; // TODO
}

int twirc_disconnect(struct twirc_state *state)
{
	twirc_cmd_quit(state);
	stcpnb_close(state->socket_fd);
	state->status = TWIRC_STATUS_DISCONNECTED;
	return 1; // TODO
}

/*
 * Returns a pointer to an initialized twirc_state struct
 * or NULL if the attempt to create a socket failed.
 */
struct twirc_state* twirc_init()
{
	// Init state struct
	struct twirc_state *state = malloc(sizeof(struct twirc_state));
	memset(state, 0, sizeof(struct twirc_state));

	// Set some defaults / initial values
	state->status = TWIRC_STATUS_DISCONNECTED;
	state->ip_type = TWIRC_IPV4;
	state->socket_fd = -1;

	// Create socket
	state->socket_fd = stcpnb_create(state->ip_type);
	if (state->socket_fd < 0)
	{
		// Socket could not be created
		return NULL;
	}

	// All worked out, let's set the appropriate fields
	return state;
}

int twirc_free(struct twirc_state *state)
{
	// no members to free... yet
	free(state);
	return 1; // TODO
}

int twirc_kill(struct twirc_state *state)
{
	if (state->status == TWIRC_STATUS_CONNECTED)
	{
		twirc_disconnect(state);
	}
	twirc_free(state);
	return 1; // TODO
}

int twirc_loop(struct twirc_state *state)
{
	// TODO
	return 0;	
}

/*
 * main
 */
int main(void)
{
	fprintf(stderr, "Starting up %s version %o.%o build %f\n",
		TWIRC_NAME, TWIRC_VER_MAJOR, TWIRC_VER_MINOR, TWIRC_VER_BUILD);

	struct twirc_state *s = twirc_init();
	if (s == NULL)
	{
		fprintf(stderr, "Could not init twirc state\n");
		return EXIT_FAILURE;
	}

	fprintf(stderr, "Successfully initialized twirc state...\n");

	int epfd = epoll_create(1);
	if (epfd < 0)
	{
		perror("Could not create epoll file descriptor");
		return EXIT_FAILURE;
	}

	struct epoll_event eev = { 0 };
	eev.data.ptr = s;
	eev.events = EPOLLRDHUP | EPOLLOUT | EPOLLIN | EPOLLET;
	int epctl_result = epoll_ctl(epfd, EPOLL_CTL_ADD, s->socket_fd, &eev);
	
	if (epctl_result)
	{
		perror("Socket could not be registered for IO");
		return EXIT_FAILURE;
	}

	fprintf(stderr, "Socket registered for IO...\n");

	if (twirc_connect(s, "irc.chat.twitch.tv", "6667") == 0)
	{
		fprintf(stderr, "Could not connect socket\n");
		return EXIT_FAILURE;
	}

	if (errno = EINPROGRESS)
	{
		fprintf(stderr, "Connection initiated...\n");
	}

	struct epoll_event epev;

	int running = 1;
	int auth = 0;
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
			fprintf(stderr, "*socket ready for reading*\n");
			twirc_recv(state);
		}

		if (epev.events & EPOLLOUT)
		{
			struct twirc_state *state = ((struct twirc_state*) epev.data.ptr);
			if (state->status == TWIRC_STATUS_CONNECTING)
			{
				int connection_status = stcpnb_status(state->socket_fd);
				if (connection_status == 0)
				{
					fprintf(stderr, "Looks like we're connected!\n");
					state->status = TWIRC_STATUS_CONNECTED;
				}
				if (connection_status == -1)
				{
					fprintf(stderr, "Socket not ready (yet)\n");

				}
				if (connection_status == -2)
				{
					fprintf(stderr, "Could not get socket status\n");
				}
				//stcpnb_status(state->socket_fd);
				if (auth == 0)
				{
					fprintf(stderr, "Authenticating...\n");
					twirc_auth(state, "kaulmate", "oauth:abc123");
					auth = 1;
				}
			}
			//twirc_sock_snd(state, "PING", 5);
		}

		if (epev.events & EPOLLRDHUP)
		{
			fprintf(stderr, "EPOLLRDHUP (peer closed socket connection)\n");
			struct twirc_state *state = ((struct twirc_state*) epev.data.ptr);
			state->status = TWIRC_STATUS_DISCONNECTED;
			running = 0;
		}

		if (epev.events & EPOLLHUP) // will fire, even if not added explicitly
		{
			fprintf(stderr, "EPOLLHUP (peer closed channel)\n");
			struct twirc_state *state = ((struct twirc_state*) epev.data.ptr);
			state->status = TWIRC_STATUS_DISCONNECTED;
			running = 0;
		}

	}

	twirc_kill(s);
	fprintf(stderr, "Bye!\n");

	close(epfd);
	return EXIT_SUCCESS;
}


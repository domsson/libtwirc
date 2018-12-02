#include <stdio.h>	// NULL, fprintf(), perror()
#include <stdlib.h>	// NULL, EXIT_FAILURE, EXIT_SUCCESS
#include <errno.h>	// errno
#include <netdb.h>	// getaddrinfo()
#include <unistd.h>	// close(), fcntl()
#include <string.h>	// strerror()
#include <fcntl.h>	// fcntl()
#include <ctype.h>	// isspace()
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

/*
 * Initiates a connection with the given server.
 * Returns  0 if connection is in progress
 * Returns -1 if connection attempt failed (check errno!)
 * Returns -2 if host/port could not be resolved to IP
 */
int twirc_connect(struct twirc_state *state, const char *host, const char *port)
{
	int con = stcpnb_connect(state->socket_fd, state->ip_type, host, port);
	if (con == 0)
	{
		state->status = TWIRC_STATUS_CONNECTING;
	}
	return con;
}

/*
 * Sends data to the IRC server, using the state's socket.
 * On succcess, returns the number of bytes sent.
 * On error, -1 is returned and errno is set appropriately.
 */
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

	if (strncmp(msg, "PASS", 4) != 0)
	{
		fprintf(stderr, "twirc_send (%d): %s\n", strlen(buf), buf);
	}

	int ret = stcpnb_send(state->socket_fd, buf, buf_len);
	free(buf);

	return ret;
}

// https://faq.cprogramming.com/cgi-bin/smartfaq.cgi?id=1044780608&answer=1108255660
/*
 * Reads data from the socket and puts it into buf.
 * On success, returns the number of bytes read.
 * If no more data is left to read, returns 0.
 * If an error occured, -1 will be returned (check errno),
 * this usually means the connection has been lost or the 
 * socket is not valid (anymore).
 */
int twirc_recv(struct twirc_state *state, char *buf, size_t len)
{
	// Make sure there is no garbage in the buffer
	memset(buf, 0, len);
	
	// Receive data
	ssize_t res_len;
	res_len = stcpnb_receive(state->socket_fd, buf, len - 1);

	// Make sure that the data received is null terminated
	buf[res_len] = '\0'; // TODO Do we need this? we already memset()

	if (res_len == -1)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			// No more data to read right now!
			return 0;
		}
		return -1;
		/*
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
		*/	
	}

	/* 
	if (res_len >= len)
	{
		fprintf(stderr, "twirc_recv: (message truncated)");
	}
	*/
	return res_len;
}

/*
 * Authenticates with the Twitch Server using the NICK and PASS commands.
 * You are not automatically authenticated when this function returns,
 * you need to wait for the server's reply (MOTD) first.
 * Returns 0 if both commands were send successfully, -1 on error.
 * TODO: See if we can't send both commands in one - what's better?
 */
int twirc_auth(struct twirc_state *state, const char *nick, const char *pass)
{
	char msg_pass[TWIRC_BUFFER_SIZE];
	snprintf(msg_pass, TWIRC_BUFFER_SIZE, "PASS %s", pass);

	char msg_nick[TWIRC_BUFFER_SIZE];
	snprintf(msg_nick, TWIRC_BUFFER_SIZE, "NICK %s", nick);

	if (twirc_send(state, msg_pass, TWIRC_BUFFER_SIZE) == -1)
	{
		return -1;
	}
	if (twirc_send(state, msg_nick, TWIRC_BUFFER_SIZE) == -1)
	{
		return -1;
	}
	return 0;
}

/*
 * Sends the QUIT command to the IRC server.
 * Returns 0 on success, -1 otherwise. 
 */
int twirc_cmd_quit(struct twirc_state *state)
{
	char msg[TWIRC_BUFFER_SIZE];
	snprintf(msg, TWIRC_BUFFER_SIZE, "QUIT");
	return twirc_send(state, msg, TWIRC_BUFFER_SIZE);
}

/*
 * Sends the QUIT command to the server, then terminates the connection.
 * Returns 0 on success, -1 on error (see errno).
 */ 
int twirc_disconnect(struct twirc_state *state)
{
	twirc_cmd_quit(state);
	int ret = stcpnb_close(state->socket_fd);
	state->status = TWIRC_STATUS_DISCONNECTED;
	return ret;
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

/*
 * Frees the twirc_state and all of its members.
 */
int twirc_free(struct twirc_state *state)
{
	// no members to free... yet
	free(state);
	return 0;
}

/*
 * Schwarzeneggers the connection with the server and frees the twirc_state.
 */
int twirc_kill(struct twirc_state *state)
{
	if (state->status == TWIRC_STATUS_CONNECTED)
	{
		twirc_disconnect(state);
	}
	twirc_free(state);
	return 0; 
}

/*
 * TODO
 */
int twirc_loop(struct twirc_state *state)
{
	return 0;	
}

/*
 * TMP
 */
int read_token(char *buf, size_t len)
{
	FILE *fp;
	fp = fopen ("token", "r");
	if (fp == NULL)
	{
		return 0;
	}
	char *res = fgets(buf, len, fp);
	if (res == NULL)
	{
		fclose(fp);
		return 0;
	}
	size_t res_len = strlen(buf);
	if (buf[res_len-1] == '\n')
	{
		buf[res_len-1] = '\0';
	}
	fclose (fp);
	return 1;
}

/*
 * Copies the contents of dest into src, but only until it finds the first 
 * null terminator. Returns the number of bytes copied + 1, so this value 
 * can be used as an offset for successive calls of this function.
 * Starts reading from the offset given in off. 
 * The copied line will be stripped of IRC line endings (\r\n).
 */
size_t pop_first_line(char *dest, size_t len, const char *src, size_t off)
{
	// Copy everything from offset to the next null terminator
	char *cpy = strncpy(dest, src + off, len);
	// Make sure the copied string is terminated (in case of buffer overflow)
	cpy[len-1] = '\0';
	// Check how many bytes we actually copied
	size_t cpy_len = strlen(cpy);
	// Remove IRC line endings from the end of the string
	if (cpy_len > 2 && isspace(cpy[cpy_len-2]))
	{
		cpy[cpy_len - 2] = '\0';
	}
	if (cpy_len > 1 && isspace(cpy[cpy_len-1]))
	{
		cpy[cpy_len - 1] = '\0';
	}
	// Return offset to the next line
	// That means we need to add 1 for the null terminator
	return off + cpy_len + 1;
}

void tmp_dump_lines(char *buf, size_t len)
{
	char line[TWIRC_BUFFER_SIZE];
	line[0] = '\0';
	size_t off = 0;
	int lc = 0;
	while ((off = pop_first_line(line, TWIRC_BUFFER_SIZE, buf, off)) <= len)
	{
		fprintf(stderr, "%d (%d): %s\n", ++lc, off, line);
		memset(line, 0, TWIRC_BUFFER_SIZE);
	}
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

	char token[128];
	int token_success = read_token(token, 128);
	if (token_success == 0)
	{
		fprintf(stderr, "Could not read token file\n");
		return EXIT_FAILURE;
	}

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

	if (twirc_connect(s, "irc.chat.twitch.tv", "6667") != 0)
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
	int joined = 0;
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
			char buf[1024];
			int bytes_received = 0;
			while ((bytes_received = twirc_recv(state, buf, 1024)) > 0)
			{
				tmp_dump_lines(buf, bytes_received);

			}
			if (!joined)
			{
				char join[] = "JOIN #domsson";
				twirc_send(s, join, strlen(join));
				joined = 1;
			}
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
				if (auth == 0)
				{
					fprintf(stderr, "Authenticating...\n");
					twirc_auth(state, "kaulmate", token);
					auth = 1;
				}
			}
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


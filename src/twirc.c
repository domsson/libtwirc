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

#define TWIRC_BUFFER_SIZE 64 * 1024

struct twirc_state
{
	int status;
	int ip_type;
	int socket_fd;
	char *buffer;
	struct twirc_events *events;
};

typedef void (*twirc_event)(struct twirc_state *s, char *msg);

struct twirc_events
{
	twirc_event connect;
	twirc_event message;
	twirc_event join;
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
int twirc_send(struct twirc_state *state, const char *msg)
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

	if (twirc_send(state, msg_pass) == -1)
	{
		return -1;
	}
	if (twirc_send(state, msg_nick) == -1)
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
	return twirc_send(state, msg);
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
	state->buffer = malloc(TWIRC_BUFFER_SIZE * sizeof(char));
	state->buffer[0] = '\0';
	state->events = NULL;

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
	free(state->buffer);
	free(state->events);
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
 * TMP: reads the oauth token for the bot from a file.
 * Obviously, this data will later be provided by the user.
 * Once we're done with the library, this function can and
 * should be deleted.
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
 * Copies a portion of src to dest. The copied part will start from the offset given
 * in off and end at the next null terminator encountered, but at most slen bytes. 
 * It is essential for slen to be correctly provided, as this function is supposed to
 * handle src strings that were network-transmitted and might not be null-terminated.
 * The copied string will be null terminated, even if no null terminator was read from
 * src. If dlen is not sufficient to store the copied string plus the null terminator,
 * or if the given offset points to the end or beyond src, -1 is returned.
 * Returns the number of bytes copied + 1, so this value can be used as an offset for 
 * successive calls of this function, or slen if all chunks have been read. 
 */
size_t shift_chunk(char *dest, size_t dlen, const char *src, size_t slen, size_t off)
{
	/*
	 | recv() 1    | recv() 2       |
	 |-------------|----------------|
	 | SOMETHING\r | \n\0ELSE\r\n\0 |
	
	 chunk 1 -> SOMETHING\0
	 chunk 2 -> \0
	 chunk 3 -> ELSE\0
	
	 | recv(1)     | recv() 2      |
	 |-------------|---------------|
	 | SOMETHING\r | \nSTUFF\r\n\0 |
	
	 chunk 1 -> SOMETHING\0
	 chunk 2 -> \nSTUFF\0
	*/
	
	// Determine the maximum number of bytes we'll have to read
	size_t chunk_len = slen - off;
	
	// Invalid offset (or maybe we're just done reading src)
	if (chunk_len <= 0)
	{
		return -1;
	}
	
	// Figure out the maximum size we can read
	size_t max_len = dlen < chunk_len ? dlen : chunk_len;
	
	// Copy everything from offset to (and including) the next null terminator
	// but at most max_len bytes (important if there is no null terminator)
	strncpy(dest, src + off, max_len); // TODO can simplify by using stpncpy()
	
	// Check how much we've actually written to the dest buffer
	size_t cpy_len = strnlen(dest, max_len);
	
	// Check if we've entirely filled the buffer (no space for \0)
	if (cpy_len == dlen)
	{
		dest[0] = '\0';
		return -1;
	}
	
	// Make sure there is a null terminator at the end, even if
	// there was no null terminator in the copied string
	dest[cpy_len] = '\0';
	
	// Calculate the offset to the next chunk (+1 to skip null terminator)
	size_t new_off = off + cpy_len + 1;
	
	// If we've read all data, return slen, otherwise the offset to the next chunk
	return new_off >= slen ? slen : new_off;
}

/*
 * Looks for a complete IRC command (ends in '\r\n') in src, then copies it over
 * to dest. The copied part will be removed from src.
 * Returns the size of the copied substring, or 0 if no complete command was found
 * in src.
 */
size_t shift_msg(char *dest, char *src)
{
	// Find the first occurence of a line break
	char *crlf = strstr(src, "\r\n");
	if (crlf == NULL)
	{
		return 0;
	}
	
	// Figure out the length of the first command
	size_t src_len = strlen(src);
	size_t end_len = strlen(crlf);
	size_t msg_len = src_len - end_len;
	
	// Copy the command to the dest buffer
	strncpy(dest, src, msg_len);
	dest[msg_len] = '\0';

	// Remove the copied part from src
	memmove(src, crlf + 2, end_len - 2);

	// Make sure src is null terminated again
	src[end_len-2] = '\0';

	return msg_len;
}	

// TODO
// https://ircv3.net/specs/core/message-tags-3.2.html
int twirc_process_msg(struct twirc_state *state, const char *msg)
{
	fprintf(stderr, "> %s (%d)\n", msg, strlen(msg));

}

/*
 * Process the received data, stored in buf, with a length of bytes_received.
 * Incomplete commands should be buffered in state->buffer, complete commands
 * should be processed right away.
 * TODO
 */
int twirc_process_data(struct twirc_state *state, const char *buf, size_t bytes_received)
{
	/*
	    +----------------------+----------------+
	    |       recv() 1       |    recv() 2    |
	    +----------------------+----------------+
	    |USER MYNAME\r\n\0PASSW|ORD MYPASS\r\n\0|
	    +----------------------+----------------+

	    -> shift_chunk() 1.1 => "USER MYNAME\r\n\0"
	    -> shift_chunk() 1.2 => "PASSW\0"
	    -> shift_chunk() 2.1 => "ORD MYPASS\r\n\0"
	*/

	char chunk[1024];
	chunk[0] = '\0';
	int off = 0;
	int lc = 0;
	

	// Here, we'll get one chunk at a time, where a chunk is a part of the
	// recieved bytes that ends in a null terminator. We'll add all of the 
	// extracted chunks to the buffer, which might already contain parts of
	// an incomplete IRC command. TODO we'll need to make sure that  the 
	// buffer is sufficiently big to contain more than one 'delivery' in
	// case one of them is incomplete, but the next one is (plus brings 
	// what was missing of the first one).

	while ((off = shift_chunk(chunk, TWIRC_BUFFER_SIZE, buf, bytes_received, off)) > 0)
	{
		// Concatenate the current buffer and the newly extracted chunk
		strcat(state->buffer, chunk);
	}

	// Here, we're lookin at each IRC command in the buffer. We do so by 
	// getting the string from the beginning of the buffer that ends in 
	// '\r\n', if any. This string will then be deleted from the buffer. 
	// We do this repeatedly until we've extracted all complete commands 
	// from the buffer. If the last bit of the buffer was an incomplete 
	// command (did not end in '\r\n'), it will be left in the buffer. 
	// Hopefully, successive recv() calls will bring in the missing 
	// pieces. If not, we will run into issues...
	
	char msg[1024];
	while (shift_msg(msg, state->buffer) > 0)
	{
		twirc_process_msg(state, msg);
	}
}

/*
 * TODO
 */
int twirc_loop(struct twirc_state *state)
{

	return 0;	
}

void handle_connect(struct twirc_state *state, char *msg)
{
	fprintf(stderr, "handle_connect()\n");
};

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

	struct twirc_events e;
	memset(&e, 0, sizeof(e));	
	e.connect = handle_connect;
	s->events = &e;

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

	size_t n = 0;
	socklen_t m = sizeof(n);
	getsockopt(s->socket_fd, SOL_SOCKET, SO_RCVBUF, (void *)&n, &m);
	
	fprintf(stderr, "recv() wants a buffer of size %d bytes\n", n);

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
	int hello = 0;
	while (running)
	{
		int num_events = epoll_wait(epfd, &epev, 1, 1 * 1000);

		if (num_events == -1)
		{
			perror("epoll_wait encountered an error");
		}

		if (num_events == 0)
		{
			continue;
		}

		if (epev.events & EPOLLIN)
		{
			struct twirc_state *state = ((struct twirc_state*) epev.data.ptr);
			fprintf(stderr, "*socket ready for reading*\n");
			char buf[4096];
			int bytes_received = 0;
			while ((bytes_received = twirc_recv(state, buf, 4096)) > 0)
			{
				twirc_process_data(state, buf, bytes_received);
			}
			if (!joined)
			{
				char join[] = "JOIN #domsson";
				twirc_send(s, join);
				joined = 1;
			}
			if (joined && !hello)
			{
				char world[] = "PRIVMSG #domsson :jobruce is the best!";
				twirc_send(s, world);
				hello = 1;
			}
		}

		if (epev.events & EPOLLOUT)
		{
			struct twirc_state *state = ((struct twirc_state*) epev.data.ptr);
			fprintf(stderr, "*socket ready for writing*\n");
			if (state->status == TWIRC_STATUS_CONNECTING)
			{
				int connection_status = stcpnb_status(state->socket_fd);
				if (connection_status == 0)
				{
					fprintf(stderr, "Looks like we're connected!\n");
					state->status = TWIRC_STATUS_CONNECTED;
					state->events->connect(state, "hello callback function");
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


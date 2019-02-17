#include <stdio.h>	// NULL, fprintf(), perror()
#include <stdlib.h>	// NULL, EXIT_FAILURE, EXIT_SUCCESS
#include <errno.h>	// errno
#include <netdb.h>	// getaddrinfo()
#include <unistd.h>	// close(), fcntl()
#include <string.h>	// strlen(), strerror()
#include <fcntl.h>	// fcntl()
#include <ctype.h>	// isspace()
#include <sys/types.h>	// ssize_t
#include <sys/socket.h> // socket(), connect(), send(), recv()
#include <sys/epoll.h>  // epoll_create(), epoll_ctl(), epoll_wait()
#include "stcpnb.c"
#include "libtwirc.h"

/*
 * Initiates a connection with the given server.
 * Returns  0 if connection is in progress
 * Returns -1 if connection attempt failed (check errno!)
 * Returns -2 if host/port could not be resolved to IP
 * TODO need more return values because epoll initalization is now in here
 *      or maybe handle errors in an entirely different way, I don't know
 */
int twirc_connect(struct twirc_state *state, const char *host, const char *port, const char *pass, const char *nick)
{
	// Create socket
	state->socket_fd = stcpnb_create(state->ip_type);
	if (state->socket_fd < 0)
	{
		// Socket could not be created
		return -3;
	}

	// Create epoll instance 
	state->epfd = epoll_create(1);
	if (state->epfd < 0)
	{
		// Could not create epoll instance / file descriptor
		return -4;
	}

	// Set up the epoll instance
	struct epoll_event eev = { 0 };
	eev.data.ptr = state;
	eev.events = EPOLLRDHUP | EPOLLOUT | EPOLLIN | EPOLLET;
	int epctl_result = epoll_ctl(state->epfd, EPOLL_CTL_ADD, state->socket_fd, &eev);
	
	if (epctl_result)
	{
		// Socket could not be registered for IO
		return -5;
	}

	// fprintf(stderr, "Socket registered for IO...\n");

	// Check the required buffer size for recv() as reported by getsockopt()
	// TODO take this out later
	size_t n = 0;
	socklen_t m = sizeof(n);
	getsockopt(state->socket_fd, SOL_SOCKET, SO_RCVBUF, (void *)&n, &m);
	fprintf(stderr, "recv() wants a buffer of size %zu bytes\n", n);

	// Properly initialize the login struct and copy the login data into it
	state->login.host = strdup(host);
	state->login.port = strdup(port);
	state->login.nick = strdup(nick);
	state->login.pass = strdup(pass);

	// Connect the socket
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

	// TODO take this out later on
	if (strncmp(msg, "PASS", 4) != 0)
	{
		fprintf(stderr, "twirc_send (%zu): %s\n", strlen(buf), buf);
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
 * TODO: Check if pass begins with "oauth:" and prefix it otherwise.
 */
int twirc_cmd_pass(struct twirc_state *state, const char *pass)
{
	char msg[TWIRC_BUFFER_SIZE];
	snprintf(msg, TWIRC_BUFFER_SIZE, "PASS %s", pass);
	return twirc_send(state, msg);
}

int twirc_cmd_nick(struct twirc_state *state, const char *nick)
{
	char msg[TWIRC_BUFFER_SIZE];
	snprintf(msg, TWIRC_BUFFER_SIZE, "NICK %s", nick);
	return twirc_send(state, msg);
}

/*
 * TODO: Check if chan begins with "#" and prefix it otherwise.
 */
int twirc_cmd_join(struct twirc_state *state, const char *chan)
{
	char msg[TWIRC_BUFFER_SIZE];
	snprintf(msg, TWIRC_BUFFER_SIZE, "JOIN %s", chan);
	return twirc_send(state, msg);
}

/*
 * TODO: Check if chan begins with "#" and prefix it otherwise.
 */
int twirc_cmd_part(struct twirc_state *state, const char *chan)
{
	char msg[TWIRC_BUFFER_SIZE];
	snprintf(msg, TWIRC_BUFFER_SIZE, "PART %s", chan);
	return twirc_send(state, msg);
}

/*
 * TODO: Check if chan begins with "#" and prefix it otherwise.
 */
int twirc_cmd_privmsg(struct twirc_state *state, const char *chan, const char *message)
{
	char msg[TWIRC_BUFFER_SIZE];
	snprintf(msg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :%s", chan, message);
	return twirc_send(state, msg);
}

/*
 * Sends the PONG command to the IRC server.
 * Returns 0 on success, -1 otherwise.
 * TODO: do we need to append " :tmi.twitch.tv"?
 */
int twirc_cmd_pong(struct twirc_state *state)
{
	return twirc_send(state, "PONG");
}

/*
 * Sends the QUIT command to the IRC server.
 * Returns 0 on success, -1 otherwise. 
 */
int twirc_cmd_quit(struct twirc_state *state)
{
	return twirc_send(state, "QUIT");
}

/*
 * Authenticates with the Twitch Server using the NICK and PASS commands.
 * You are not automatically authenticated when this function returns,
 * you need to wait for the server's reply (MOTD) first.
 * Returns 0 if both commands were send successfully, -1 on error.
 * TODO: See if we can't send both commands in one - what's better?
 */
int twirc_auth(struct twirc_state *state)
{
	if (twirc_cmd_pass(state, state->login.pass) == -1)
	{
		return -1;
	}
	if (twirc_cmd_nick(state, state->login.nick) == -1)
	{
		return -1;
	}
	return 0;
}

/*
 * Sends the QUIT command to the server, then terminates the connection.
 * Returns 0 on success, -1 on error (see errno).
 */ 
int twirc_disconnect(struct twirc_state *state)
{
	fprintf(stderr, "Disconnecting...\n");
	twirc_cmd_quit(state);
	int ret = stcpnb_close(state->socket_fd);
	state->status = TWIRC_STATUS_DISCONNECTED;
	return ret;
}

/*
 * Set the state's events member to the given pointer.
 */
void twirc_set_callbacks(struct twirc_state *state, struct twirc_events *events)
{
	memcpy(&state->events, events, sizeof(struct twirc_events));
}

/*
 * TODO comment
 */
struct twirc_state* twirc_init(struct twirc_events *events)
{
	// Init state struct
	struct twirc_state *state = malloc(sizeof(struct twirc_state));
	memset(state, 0, sizeof(struct twirc_state));

	// Set some defaults / initial values
	state->status = TWIRC_STATUS_DISCONNECTED;
	state->ip_type = TWIRC_IPV4;
	state->socket_fd = -1;
	
	// Initialize the buffer - it will be twice the message size so it can
	// easily hold an incomplete message in addition to a complete one
	state->buffer = malloc(2 * TWIRC_MESSAGE_SIZE * sizeof(char));
	state->buffer[0] = '\0';
	
	// Make sure the structs within state are zero-initialized
	memset(&state->login, 0, sizeof(struct twirc_login));
	memset(&state->events, 0, sizeof(struct twirc_events));

	// Copy the provided events 
	twirc_set_callbacks(state, events);

	// All done
	return state;
}

int twirc_free_events(struct twirc_state *state)
{
	// TODO free all the members!
	// free(state->events->...);
	// state->events = NULL;
	return 0;
}

int twirc_free_login(struct twirc_state *state)
{
	free(state->login.host);
	free(state->login.port);
	free(state->login.nick);
	free(state->login.pass);
	return 0;
}

/*
 * Frees the twirc_state and all of its members.
 */
int twirc_free(struct twirc_state *state)
{
	twirc_free_events(state);
	twirc_free_login(state);
	free(state->buffer);
	free(state);
	state = NULL;
	return 0;
}

/*
 * Schwarzeneggers the connection with the server and frees the twirc_state.
 */
int twirc_kill(struct twirc_state *state)
{
	if (state->status & TWIRC_STATUS_CONNECTED)
	{
		twirc_disconnect(state);
	}
	close(state->epfd);
	twirc_free(state);
	return 0; 
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
size_t libtwirc_next_chunk(char *dest, size_t dlen, const char *src, size_t slen, size_t off)
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
 * Finds the first occurrence of the sep string in src, then copies everything 
 * that is found before the separator into dest. The extracted part and the 
 * separator will be removed from src. Note that this function will by design
 * not extract the last token, unless it also ends in the given separator.
 * This is so because it was designed to extract complete IRC messages from a 
 * string containing multiple of those, where the last one might be incomplete.
 * Only extracting tokens that _end_ in the separator guarantees that we will
 * only extract complete IRC messages and leave incomplete ones in src.
 *
 * Returns the size of the copied string, or 0 if the separator could not be 
 * found in the src string.
 */
size_t libtwirc_shift_token(char *dest, char *src, const char *sep)
{
	// Find the first occurence of the separator
	char *sep_pos = strstr(src, sep);
	if (sep_pos == NULL)
	{
		return 0;
	}

	// Figure out the length of the token etc
	size_t sep_len = strlen(sep);
	size_t new_len = strlen(sep_pos) - sep_len;
	size_t tok_len = sep_pos - src;
	
	// Copy the token to the dest buffer
	strncpy(dest, src, tok_len);
	dest[tok_len] = '\0';

	// Remove the token from src
	memmove(src, sep_pos + sep_len, new_len); 

	// Make sure src is null terminated again
	src[new_len] = '\0';

	return tok_len;
}

/*
 * Finds the first occurrence of sep within src, then copies everything before
 * the sep into dest. Returns a pointer to the string after the separator or 
 * NULL if the separator was not found in src.
 * TODO: doesn't this return a pointer past src once no sep is found anymore?
 *       also, if we return NULL when there is no separator, we can never get
 *       to the last token, can we? This is okay for shift_token, as the idea
 *       behind it is to work on a string made up of multiple IRC messages,
 *       where even the last one ends in '\r\n', but now that we've made this
 *       into a rather general-purpose function that takes the seperator as 
 *       an input parameter, this behavior is rather counter-intuitive, no?
 */
char *libtwirc_next_token(char *dest, const char *src, const char *sep)
{
	// Find the first occurence of the separator
	char *sep_pos = strstr(src, sep);
	if (sep_pos == NULL)
	{
		return NULL;
	}

	// Figure out the length of the token
	size_t tok_len = sep_pos - src;

	// Copy the token to the dest buffer
	strncpy(dest, src, tok_len);
	dest[tok_len] = '\0';

	// Return a pointer to the remaining string
	return sep_pos + strlen(sep);
}

struct twirc_tag *libtwirc_create_tag(const char *key, const char *val)
{
	struct twirc_tag *tag = malloc(sizeof(struct twirc_tag));
	tag->key   = key ? strdup(key) : NULL;
	tag->value = val ? strdup(val) : NULL;
	return tag;
}

void libtwirc_destroy_tag(struct twirc_tag *tag)
{
	free(tag->key);
	free(tag->value);
	free(tag);
	tag = NULL;
}

void libtwirc_destroy_tags(struct twirc_tag **tags)
{
	if (tags == NULL)
	{
		return;
	}
	for (int i = 0; tags[i] != NULL; ++i)
	{
		libtwirc_destroy_tag(tags[i]);
	}
	free(tags);
	tags = NULL;
}

/*
 * Extracts tags from the beginning of an IRC message, if any, and returns them
 * as a pointer to a dynamically allocated array of twirc_tag structs, where 
 * each struct contains two members, key and value, representing the key and 
 * value of a tag, respectively. The value member of a tag can be NULL for tags 
 * that are key-only. The last element of the array will be a NULL pointer, so 
 * you can loop over all tags until you hit NULL. The number of extracted tags
 * is not returned. If no tags have been found at the beginning of msg, NULL 
 * will be returned. 
 *
 * TODO un-escape the tag values before saving them in the tag structs!
 *
 * https://ircv3.net/specs/core/message-tags-3.2.html
 */
struct twirc_tag **libtwirc_parse_tags(char *msg)
{
	// If msg doesn't start with "@", then there are no tags
	if (msg[0] != '@')
	{
		return NULL;
	}

	// Get everything until the next space (+1 to skip the "@" prefix)
	// This will also remove the extracted part from msg!
	char tag_str[1024];
	libtwirc_shift_token(tag_str, msg + 1, " ");
	
	size_t num_tags = TWIRC_NUM_TAGS;
	struct twirc_tag **tags = malloc(num_tags * sizeof(struct twirc_tag));
	memset(tags, 0, num_tags * sizeof(struct twirc_tag));

	char *tag;
	int i;
	for (i = 0; (tag = strtok(i == 0 ? tag_str : NULL, ";")) != NULL; ++i)
	{
		// Make sure we have enough space; last element has to be NULL
		if (i >= num_tags - 1)
		{
			size_t additional = num_tags * 0.5;
			num_tags += additional;
			tags = realloc(tags, num_tags * sizeof(struct twirc_tag));
			memset(tags + i + 1, 0, additional * sizeof(struct twirc_tag));
		}

		char *eq = strstr(tag, "=");
		
		// It's a key-only tag
		if (eq == NULL)
		{
			tags[i] = libtwirc_create_tag(tag, NULL);
		}
		// It's a tag with key-value pair
		else
		{
			eq[0] = '\0';
			tags[i] = libtwirc_create_tag(tag, eq+1);
		}
	}

	// TODO should we re-alloc to use exactly the amount of memory we need
	// for the number of tags extracted (i), or would it be faster to just
	// go with what we have? In other words, CPU vs. memory, which one?

	return tags; 
}

/*
 * Extracts the prefix from the beginning of msg, if there is one. The prefix 
 * will be returned as a pointer to a dynamically allocated string. The caller
 * needs to make sure to free the memory at some point. If no prefix was found 
 * at the beginning of msg, a null pointer is returned.
 */
char *libtwirc_parse_prefix(char *msg)
{
	if (msg[0] != ':')
	{
		return NULL;
	}

	// Get everything until the next space (+1 to skil the ":" prefix)
	// This will also remove the extracted part from msg!
	char *prefix = malloc(TWIRC_PREFIX_SIZE * sizeof(char));
	size_t prefix_len = libtwirc_shift_token(prefix, msg + 1, " ");

	// Trim the used memory to only what we actually need
	prefix = realloc(prefix, prefix_len + 1);

	return prefix;
}

char *libtwirc_parse_command(char *msg)
{
	return NULL;
}

char *libtwirc_parse_params(char *msg)
{
	return NULL;
}

// TODO
int libtwirc_process_msg(struct twirc_state *state, char *msg)
{
	// ALL OF THIS IS MOSTLY TEMPORARY TEST CODE
	fprintf(stderr, "> %s (%zu)\n", msg, strlen(msg));

	if (strstr(msg, ":tmi.twitch.tv 001 ") != NULL)
	{
		state->status |= TWIRC_STATUS_AUTHENTICATED;
		if (state->events.welcome != NULL)
		{
			state->events.welcome(state, msg);
		}
	}
	if (strstr(msg, "tmi.twitch.tv JOIN #") != NULL)
	{
		if (state->events.join != NULL)
		{
			state->events.join(state, msg);
		}
	}
	if (strstr(msg, "PING :tmi.twitch.tv") != NULL)
	{
		twirc_send(state, "PONG :tmi.twitch.tv");
		if (state->events.ping != NULL)
		{
			state->events.ping(state, msg);
		}
	}
	
	struct twirc_tag **tags = libtwirc_parse_tags(msg);
	char *prefix = libtwirc_parse_prefix(msg);

	libtwirc_destroy_tags(tags);
	free(prefix);

	return 0; // TODO
}

/*
 * Process the received data in buf, which has a size of len bytes.
 * Incomplete commands will be buffered in state->buffer, complete commands
 * will be processed right away.
 * TODO
 */
int libtwirc_process_data(struct twirc_state *state, const char *buf, size_t len)
{
	/*
	    +----------------------+----------------+
	    |       recv() 1       |    recv() 2    |
	    +----------------------+----------------+
	    |USER MYNAME\r\n\0PASSW|ORD MYPASS\r\n\0|
	    +----------------------+----------------+

	    -> libtwirc_next_chunk() 1.1 => "USER MYNAME\r\n\0"
	    -> libtwirc_next_chunk() 1.2 => "PASSW\0"
	    -> libtwirc_next_chunk() 2.1 => "ORD MYPASS\r\n\0"
	*/

	// A chunk has to be able to hold at least as much data as the buffer
	// we're working on. This means we'll dynamically allocate the chunk 
	// buffer to the same size as the handed in buffer, plus one to make
	// room for a null terminator which might not be present in the data
	// received, but will definitely be added in the chunk.

	char *chunk = malloc(len + 1);
	chunk[0] = '\0';
	int off = 0;

	// Here, we'll get one chunk at a time, where a chunk is a part of the
	// recieved bytes that ends in a null terminator. We'll add all of the 
	// extracted chunks to the buffer, which might already contain parts of
	// an incomplete IRC command. The buffer needs to be sufficiently big 
	// to contain more than one delivery in case one of them is incomplete,
	// but the next one is complete and adds the missing pieces of the 
	// previous one.

	while ((off = libtwirc_next_chunk(chunk, len + 1, buf, len, off)) > 0)
	{
		// Concatenate the current buffer and the newly extracted chunk
		strcat(state->buffer, chunk);
	}

	free(chunk);

	// Here, we're lookin at each IRC command in the buffer. We do so by 
	// getting the string from the beginning of the buffer that ends in 
	// '\r\n', if any. This string will then be deleted from the buffer.
	// We do this repeatedly until we've extracted all complete commands 
	// from the buffer. If the last bit of the buffer was an incomplete 
	// command (did not end in '\r\n'), it will be left in the buffer. 
	// Hopefully, successive recv() calls will bring in the missing pieces.
	// If not, we will run into issues, as the buffer could silently and 
	// slowly fill up until it finally overflows. TODO: test for that?
	
	char msg[TWIRC_MESSAGE_SIZE];
	msg[0] = '\0';

	while (libtwirc_shift_token(msg, state->buffer, "\r\n") > 0)
	{
		libtwirc_process_msg(state, msg);
	}
	
	return 0; // TODO
}

int libtwirc_handle_event(struct twirc_state *s, struct epoll_event *epev)
{
	// We've got data coming in
	if(epev->events & EPOLLIN)
	{
		fprintf(stderr, "*socket ready for reading*\n");
		char buf[TWIRC_BUFFER_SIZE];
		int bytes_received = 0;
		while ((bytes_received = twirc_recv(s, buf, TWIRC_BUFFER_SIZE)) > 0)
		{
			libtwirc_process_data(s, buf, bytes_received);
		}
	}
	
	// We're ready to send data
	if (epev->events & EPOLLOUT)
	{
		fprintf(stderr, "*socket ready for writing*\n");
		if (s->status & TWIRC_STATUS_CONNECTING)
		{
			// TODO: do we really need to ask stcpnb_status()?
			// When the socket is ready for writing after we made
			// a connection attempt, can't we assume that wer are
			// now connected to the server?
			int conn_status = stcpnb_status(s->socket_fd);
			if (conn_status == 0)
			{
				fprintf(stderr, "Looks like we're connected!\n");
				s->status = TWIRC_STATUS_CONNECTED;
				if (s->events.connect != NULL)
				{
					s->events.connect(s, "");
				}
			}
			if (conn_status == -1)
			{
				fprintf(stderr, "Socket not ready (yet)\n");
			}
			if (conn_status == -2)
			{
				fprintf(stderr, "Could not get socket status\n");
			}
		}
		if (s->status & TWIRC_STATUS_CONNECTED)
		{
			if ((s->status & TWIRC_STATUS_AUTHENTICATED) == 0)
			{
				fprintf(stderr, "Authenticating...\n");
				twirc_auth(s);
				s->status |= TWIRC_STATUS_AUTHENTICATING;
			}
		}
	}
	
	// Server closed the connection
	if (epev->events & EPOLLRDHUP)
	{
		fprintf(stderr, "EPOLLRDHUP (peer closed socket connection)\n");
		s->status = TWIRC_STATUS_DISCONNECTED;
		s->running = 0;
		return -1;
	}
	
	// Server closed the connection 
	if (epev->events & EPOLLHUP) // will fire, even if not added explicitly
	{
		fprintf(stderr, "EPOLLHUP (peer closed channel)\n");
		s->status = TWIRC_STATUS_DISCONNECTED;
		s->running = 0;
		return -1;
	}

	// Connection error
	if (epev->events & EPOLLERR) // will fire, even if not added explicitly
	{
		fprintf(stderr, "EPOLLERR (socket error)\n");
		s->status = TWIRC_STATUS_DISCONNECTED;
		s->running = 0;
		return -1;
	}
	
	return 0;
}

int twirc_tick(struct twirc_state *s, int timeout)
{
	struct epoll_event epev;
	//struct epoll_event events[TWIRC_MAX_EVENTS];
	
	// TODO important: do we need to account for multiple events?
	// Seeing how we have a user-defined timeout, I would definitely think so!
	// This means everything the whole event evaluation code has to be run in
	// a loop, which runs num_events times until all events have been processed!
	// It also means that we have to decide upon a max_events number (how?).
	int num_events = epoll_wait(s->epfd, &epev, 1, timeout);

	// An error has occured
	if (num_events == -1)
	{
		fprintf(stderr, "epoll_wait encountered an error\n");
		s->running = 0;
		return -1;
	}
	
	// No events have occured
	if (num_events == 0)
	{
		return 0;
	}

	return libtwirc_handle_event(s, &epev);
}

/*
 * TODO
 */
int twirc_loop(struct twirc_state *state, int timeout)
{
	state->running = 1;
	while (state->running)
	{
		// timeout is in milliseconds
		twirc_tick(state, timeout);
	}
	return 0;
}


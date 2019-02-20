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
		//fprintf(stderr, "twirc_send (%zu): %s", strlen(buf), buf);
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

char *libtwirc_unescape_tag(const char *val)
{
	size_t val_len = strlen(val);
	char *escaped = malloc((val_len + 1) * sizeof(char));
	
	int e = 0;
	for (int i = 0; i < (int) val_len - 1; ++i)
	{
		if (val[i] == '\\')
		{
			if (val[i+1] == ':') // "\:" -> ";"
			{
				escaped[e++] = ';';
				++i;
				continue;
			}
			if (val[i+1] == 's') // "\s" -> " ";
			{
				escaped[e++] = ' ';
				++i;
				continue;
			}
			if (val[i+1] == '\\') // "\\" -> "\";
			{
				escaped[e++] = '\\';
				++i;
				continue;
			}
			if (val[i+1] == 'r') // "\r" -> '\r' (CR)
			{
				escaped[e++] = '\r';
				++i;
				continue;
			}
			if (val[i+1] == 'n') // "\n" -> '\n' (LF)
			{
				escaped[e++] = '\n';
				++i;
				continue;
			}
		}
		escaped[e++] = val[i];
	}
	escaped[e] = '\0';
	return escaped;
}

struct twirc_tag *libtwirc_create_tag(const char *key, const char *val)
{
	struct twirc_tag *tag = malloc(sizeof(struct twirc_tag));
	tag->key   = strdup(key);
	tag->value = libtwirc_unescape_tag(val);
	return tag;
}

void libtwirc_free_tag(struct twirc_tag *tag)
{
	free(tag->key);
	free(tag->value);
	free(tag);
	tag = NULL;
}

void libtwirc_free_tags(struct twirc_tag **tags)
{
	if (tags == NULL)
	{
		return;
	}
	for (int i = 0; tags[i] != NULL; ++i)
	{
		libtwirc_free_tag(tags[i]);
	}
	free(tags);
	tags = NULL;
}

void libtwirc_free_params(char **params)
{
	if (params == NULL)
	{
		return;
	}
	for (int i = 0; params[i] != NULL; ++i)
	{
		free(params[i]);
	}
	free(params);
	params = NULL;
}

char *libtwirc_prefix_to_nick(const char *prefix)
{
	char *sep = strstr(prefix, "!");
	if (sep == NULL)
	{
		return NULL;
	}
	size_t len = sep - prefix;
	return strndup(prefix, len);
}


/*
 * Extracts tags from the beginning of an IRC message, if any, and returns them
 * as a pointer to a dynamically allocated array of twirc_tag structs, where 
 * each struct contains two members, key and value, representing the key and 
 * value of a tag, respectively. The value member of a tag can be NULL for tags 
 * that are key-only. The last element of the array will be a NULL pointer, so 
 * you can loop over all tags until you hit NULL. The number of extracted tags
 * is returned in len. If no tags have been found at the beginning of msg, tags
 * will be NULL, len will be 0 and this function will return a pointer to msg.
 * Otherwise, a pointer to the part of msg after the tags will be returned. 
 *
 * TODO un-escape the tag values before saving them in the tag structs!
 *
 * https://ircv3.net/specs/core/message-tags-3.2.html
 */
const char *libtwirc_parse_tags(const char *msg, struct twirc_tag ***tags, size_t *len)
{
	// If msg doesn't start with "@", then there are no tags
	if (msg[0] != '@')
	{
		*len = 0;
		*tags = NULL;
		return msg;
	}

	// Find the next space (the end of the tags string within msg)
	char *next = strstr(msg, " ");
	
	// Duplicate the string from after the '@' until the next space
	char *tag_str = strndup(msg + 1, next - (msg + 1));

	// Set the initial number of tags we want to allocate memory for
	size_t num_tags = TWIRC_NUM_TAGS;
	
	// Allocate memory in the provided pointer to ptr-to-array-of-structs
	*tags = malloc(num_tags * sizeof(struct twirc_tag*));
	//memset(*tags, 0, num_tags * sizeof(struct twirc_tag*));

	char *tag;
	int i;
	for (i = 0; (tag = strtok(i == 0 ? tag_str : NULL, ";")) != NULL; ++i)
	{
		// Make sure we have enough space; last element has to be NULL
		if (i >= num_tags - 1)
		{
			size_t add = num_tags * 0.5;
			num_tags += add;
			*tags = realloc(*tags, num_tags * sizeof(struct twirc_tag*));
			//memset(*tags + i + 1, 0, add * sizeof(struct twirc_tag*));
		}

		char *eq = strstr(tag, "=");
		
		// TODO there is a bug in here! when eq is NOT null (meaning there is
		// an equals sign in there, BUT it's a key-only tag, meaning there is
		// a null terminator just after the '=', then we actually end up with
		// the '=' attached to the key name in the tag! we need to strip it!

		// It's a key-only tag, for example "tagname" or "tagname="
		// So either we didn't find an '=' or the next char is '\0'
		if (eq == NULL || eq[1] == '\0')
		{
			(*tags)[i] = libtwirc_create_tag(tag, eq == NULL ? "" : eq+1);
		}
		// It's a tag with key-value pair
		else
		{
			eq[0] = '\0';
			(*tags)[i] = libtwirc_create_tag(tag, eq+1);
		}
		//fprintf(stderr, ">>> TAG %d: %s = %s\n", i, (*tags)[i]->key, (*tags)[i]->value);
	}

	// Set the number of tags found
	*len = i;

	free(tag_str);
	
	// TODO should we re-alloc to use exactly the amount of memory we need
	// for the number of tags extracted (i), or would it be faster to just
	// go with what we have? In other words, CPU vs. memory, which one?
	if (i < num_tags - 1)
	{
		*tags = realloc(*tags, (i + 1) * sizeof(struct twirc_tag*));
	}

	// Make sure the last element is a NULL ptr
	(*tags)[i] = NULL;

	// Return a pointer to the remaining part of msg
	return next + 1;
}

/*
 * Extracts the prefix from the beginning of msg, if there is one. The prefix 
 * will be returned as a pointer to a dynamically allocated string in prefix.
 * The caller needs to make sure to free the memory at some point. If no prefix
 * was found at the beginning of msg, prefix will be NULL. Returns a pointer to
 * the next part of the message, after the prefix.
 */
const char *libtwirc_parse_prefix(const char *msg, char **prefix)
{
	if (msg[0] != ':')
	{
		*prefix = NULL;
		return msg;
	}
	
	// Find the next space (the end of the prefix string within msg)
	char *next = strstr(msg, " ");

	// Duplicate the string from after the ':' until the next space
	*prefix = strndup(msg + 1, next - (msg + 1));

	// Return a pointer to the remaining part of msg
	return next + 1;
}

const char *libtwirc_parse_command(const char *msg, char **cmd)
{
	// Find the next space (the end of the cmd string withing msg)
	char *next = strstr(msg, " ");

	// Duplicate the string from start to next space or end of msg
	*cmd = strndup(msg, next == NULL ? strlen(msg) - 2 : next - msg);
	
	// Return NULL if cmd was the last bit of msg, or a pointer to
	// the remaining part (the parameters)
	return next == NULL ? NULL : next + 1;
}

const char *libtwirc_parse_params(const char *msg, char ***params, size_t *len, int *t_idx)
{
	if (msg == NULL)
	{
		*t_idx = -1;
		*len = 0;
		*params = NULL;
		return NULL;
	}

	// Initialize params to hold TWIRC_NUM_PARAMS pointers to char
	size_t num_params = TWIRC_NUM_PARAMS;
	*params = malloc(num_params * sizeof(char*));
	//memset(*params, 0, num_params * sizeof(char*));

	// Copy everything that's left in msg (params are always last)
	char *p_str = strdup(msg);

	size_t p_len = strlen(p_str);
	size_t num_tokens = 0;
	int trailing = 0;
	int from = 0;

	for (int i = 0; i < p_len; ++i)
	{
		// Make sure we have enough space; last element has to be NULL
		if (num_tokens >= num_params - 1)
		{
			size_t add = num_params;
			num_params += add;
			*params = realloc(*params, num_params * sizeof(char*));
			//memset(*params + i + 1, 0, add * sizeof(char*));
		}

		// Prefix of trailing token (ignore if part of trailing token)
		if (p_str[i] == ':' && !trailing)
		{
			// Remember that we're in the trailing token
			trailing = 1;
			// Set the start position marker to the next char
			from = i+1;
			continue;
		}

		// Token separator (ignore if trailing token)
		if (p_str[i] == ' ' && !trailing)
		{
			// Copy everything from the beginning to here
			(*params)[num_tokens++] = strndup(p_str + from, i - from);
			//fprintf(stderr, "- %s\n", (*params)[num_tokens-1]);
			// Set the start position marker to the next char
			from = i+1;
			continue;
		}

		// We're at the last character (null terminator, hopefully)
		if (i == p_len - 1)
		{
			// Copy everything from the beginning to here + 1
			(*params)[num_tokens++] = strndup(p_str + from, (i + 1) - from);
			//fprintf(stderr, "- %s\n", (*params)[num_tokens-1]);
		}
	}

	// Set index of the trailing parameter, if any, otherwise -1
	*t_idx = trailing ? num_tokens - 1 : -1;
	// Set number of tokens (parameters) found and copied
	*len = num_tokens;
	
	free(p_str);

	// Make sure we only use as much memory as we have to
	// TODO Figure out if this is actually worth the CPU cycles.
	//      After all, a couple of pointers don't each much RAM.
	if (num_tokens < num_params - 1)
	{
		*params = realloc(*params, (num_tokens + 1) * sizeof(char*));
	}
	
	// Make sure the last element is a NULL ptr
	(*params)[num_tokens] = NULL;

	// We've reached the end of msg, so we'll return NULL
	return NULL;
}

// TODO
int libtwirc_process_msg(struct twirc_state *state, const char *msg)
{
	//fprintf(stderr, "> %s (%zu)\n", msg, strlen(msg));

	struct twirc_message message = { 0 };

	// Extract the tags, if any
	msg = libtwirc_parse_tags(msg, &(message.tags), &(message.num_tags));
	//fprintf(stderr, ">>> num_tags: %zu\n", num_tags);

	// Extract the prefix, if any
	msg = libtwirc_parse_prefix(msg, &(message.prefix));
	//fprintf(stderr, ">>> prefix: %s\n", prefix);

	// Extract the command
	msg = libtwirc_parse_command(msg, &(message.command));
	//fprintf(stderr, ">>> cmd: %s\n", cmd);

	// Extract the parameters, if any
	msg = libtwirc_parse_params(msg, &(message.params), &(message.num_params), &(message.trailing));
	//fprintf(stderr, ">>> num_params: %zu\n", num_params);

	//fprintf(stderr, "(prefix: %s, cmd: %s, tags: %zu, params: %zu, trail: %d)\n", prefix?"y":"n", cmd, num_tags, num_params, trail_idx);

	message.nick = libtwirc_prefix_to_nick(message.prefix);

	// Some temporary test code (the first bit is important tho)
	if (strcmp(message.command, "001") == 0)
	{
		state->status |= TWIRC_STATUS_AUTHENTICATED;
		if (state->events.welcome != NULL)
		{
			state->events.welcome(state, &message);
		}

	}
	if (strcmp(message.command, "JOIN") == 0)
	{
		if (state->events.join != NULL)
		{
			message.channel = message.params[0];
			state->events.join(state, &message);
		}

	}
	if (strcmp(message.command, "PING") == 0)
	{
		char pong[128];
		pong[0] = '\0';
		snprintf(pong, 128, "PONG %s", message.params[0]);

		twirc_send(state, pong);
		if (state->events.ping != NULL)
		{
			state->events.ping(state, &message);
		}
	}
	if (strcmp(message.command, "PRIVMSG") == 0 && message.num_params >= 2)
	{
		message.channel = message.params[0];

		if (message.params[1][0] == 0x01 && message.params[1][strlen(message.params[1])-1] == 0x01)
		{
			fprintf(stderr, "[!] CTCP detected\n");
		}
		else
		{
			if (state->events.privmsg != NULL)
			{
				state->events.privmsg(state, &message);
			}
		}
	}

	// Free resources from parsed message
	libtwirc_free_tags(message.tags);
	free(message.prefix);
	free(message.nick);
	free(message.command);
	libtwirc_free_params(message.params);

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

/*
 * Handles the epoll event epev.
 * Returns 0 on success, -1 if the connection has been interrupted.
 */
int libtwirc_handle_event(struct twirc_state *s, struct epoll_event *epev)
{
	// We've got data coming in
	if(epev->events & EPOLLIN)
	{
		//fprintf(stderr, "*socket ready for reading*\n");
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
		//fprintf(stderr, "*socket ready for writing*\n");
		if (s->status & TWIRC_STATUS_CONNECTING)
		{
			/*
			// TODO: do we really need to ask stcpnb_status()?
			// When the socket is ready for writing after we made
			// a connection attempt, can't we assume that we are
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
			*/

			fprintf(stderr, "Connection established!\n");
			s->status = TWIRC_STATUS_CONNECTED;
			if (s->events.connect != NULL)
			{
				//s->events.connect(s, "");
				s->events.connect(s, NULL);
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

/*
 * TODO we probably don't need this - it seems that one single socket (and 
 * therefore one single file descriptor) never raises more than one event at a
 * time, so why have code that accounts for multiple events?
 *
 * Waits timeout milliseconds for events to happen on the IRC connection.
 * Returns 0 if all events have been handled and -1 if an error has been 
 * encountered or the connection has been lost (check the twirc_state's
 * connection status to see if the latter is the case). 
 */
int twirc_tick_n(struct twirc_state *s, int timeout)
{
	struct epoll_event events[TWIRC_MAX_EVENTS];
	int num_events = epoll_wait(s->epfd, events, TWIRC_MAX_EVENTS, timeout);

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

	//fprintf(stderr, ">>> num_events = %d\n", num_events);

	int status = 0;
	for (int i = 0; i < num_events; ++i)
	{
		status += libtwirc_handle_event(s, &events[i]);
	}

	return (status != 0) ? -1 : 0;
}

/*
 * Waits timeout milliseconds for events to happen on the IRC connection.
 * Returns 0 if all events have been handled and -1 if an error has been 
 * encountered or the connection has been lost (check the twirc_state's
 * connection status to see if the latter is the case). 
 */
int twirc_tick(struct twirc_state *s, int timeout)
{
	struct epoll_event epev;
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
 * Runs an endless loop that waits for events timeout milliseconds at a time.
 * Once the connection has been closed or the state's running field has been 
 * set to 0, the loops is ended and this function returns with a value of 0.
 */
int twirc_loop(struct twirc_state *state, int timeout)
{
	// TODO we should probably put some connection time-out code in place
	// so that we stop running after the connection attempt has been going 
	// on for so-and-so long. Or shall we leave that up to the user code?

	state->running = 1;
	while (state->running)
	{
		// timeout is in milliseconds
		twirc_tick(state, timeout);
	}
	return 0;
}


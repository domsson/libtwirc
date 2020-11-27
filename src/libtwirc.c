#define TCPSOCK_IMPLEMENTATION

#include <stdio.h>      // NULL, fprintf(), perror()
#include <stdlib.h>     // NULL, EXIT_FAILURE, EXIT_SUCCESS
#include <errno.h>      // errno
#include <unistd.h>     // close()
#include <string.h>     // strlen(), strerror()
#include <sys/epoll.h>  // epoll_create(), epoll_ctl(), epoll_wait()
#include <time.h>       // time() (as seed for rand())
#include <signal.h>     // sigset_t et al
#include "tcpsock.h"
#include "libtwirc.h"
#include "libtwirc_internal.h"
#include "libtwirc_cmds.c"
#include "libtwirc_util.c"
#include "libtwirc_evts.c"

/*
 * Sets the state's error flag to TWIRC_ERR_OUT_OF_MEMORY and returns -1.
 */
static int
libtwirc_oom(twirc_state_t *s)
{
	s->error = TWIRC_ERR_OUT_OF_MEMORY;
	return -1;
}

/*
 * Requests all supported capabilities from the Twitch servers.
 * Returns 0 if the request was sent successfully, -1 on error.
 */
static int
libtwirc_capreq(twirc_state_t *s)
{
	// TODO chatrooms cap currently not implemented!
	//      Once that's done, use the following line
	//      in favor of the other boongawoonga here.
	// return twirc_cmd_req_all(s);

	int success = 0;
	success += twirc_cmd_req_tags(s);
	success += twirc_cmd_req_membership(s);
	success += twirc_cmd_req_commands(s);
	return success == 0 ? 0 : -1;
}

/*
 * Authenticates with the Twitch Server using the NICK and PASS commands.
 * Login is not automatically completed upon return of this function, one has 
 * to wait for the server to reply. If the tags capability has been requested 
 * beforehand, the server will confirm login with the GLOBALUSERSTATE command,
 * otherwise just look out for the MOTD (starting with numeric command 001).
 * Returns 0 if both commands were send successfully, -1 on error.
 */
static int
libtwirc_auth(twirc_state_t *s)
{
	if (twirc_cmd_pass(s, s->login.pass) == -1)
	{
		return -1;
	}
	if (twirc_cmd_nick(s, s->login.nick) == -1)
	{
		return -1;
	}

	s->status |= TWIRC_STATUS_AUTHENTICATING;
	return 0;
}

static void
libtwirc_free_callbacks(twirc_state_t *s)
{
	// We do not need to free the callback pointers, as they are function 
	// pointers and were therefore not allocated with malloc() or similar.
	// However, let's make sure we 'forget' the currently assigned funcs.
	memset(&s->cbs, 0, sizeof(twirc_callbacks_t));
}

static void
libtwirc_free_login(twirc_state_t *s)
{
	free(s->login.host);
	free(s->login.port);
	free(s->login.nick);
	free(s->login.pass);
	free(s->login.name);
	free(s->login.id);
	s->login.host = NULL;
	s->login.port = NULL;
	s->login.nick = NULL;
	s->login.pass = NULL;
	s->login.name = NULL;
	s->login.id   = NULL;
}

/*
 * Copies a portion of src to dest. The copied part will start from the offset
 * given in off and end at the next null terminator encountered, but at most 
 * slen bytes. It is essential for slen to be correctly provided, as this 
 * function is supposed to handle src strings that were network-transmitted and
 * might not be null-terminated. The copied string will be null terminated, even
 * if no null terminator was read from src. If dlen is not sufficient to store 
 * the copied string plus the null terminator, or if the given offset points to 
 * the end or beyond src, -1 is returned. Returns the number of bytes copied + 1, 
 * so this value can be used as an offset for successive calls of this function, 
 * or slen if all chunks have been read. 
 */
static size_t
libtwirc_next_chunk(char *dest, size_t dlen, const char *src, size_t slen, size_t off)
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
	strncpy(dest, src + off, max_len); 
	
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
	
	// Return slen if we've read all data, otherwise the offset to the next chunk
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
static size_t
libtwirc_shift_token(char *dest, char *src, const char *sep)
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
 * Takes an escaped string (as described in the IRCv3 spec, section tags)
 * and returns a pointer to a malloc'd string that holds the unescaped string.
 * Remember that the returned pointer has to be free'd by the caller!
 * TODO: THIS CAN SEGFAULT! FIX IT!
 * TODO: ^- regarding above comment, I think we fixed the segfault and simply
 *          forgot to remove the comment... but not sure, so please re-check.
 */
static char*
libtwirc_unescape(const char *str)
{
	size_t str_len = strlen(str);
	char *unescaped = malloc((str_len + 1) * sizeof(char));
	if (unescaped == NULL) { return NULL; }

	int u = 0;
	for (int i = 0; i < (int) str_len; ++i)
	{
		if (str[i] == '\\')  
		{
			if (str[i+1] == ':') // "\:" -> ";"
			{
				unescaped[u++] = ';';
				++i;
				continue;
			}
			if (str[i+1] == 's') // "\s" -> " ";
			{
				unescaped[u++] = ' ';
				++i;
				continue;
			}
			if (str[i+1] == '\\') // "\\" -> "\";
			{
				unescaped[u++] = '\\';
				++i;
				continue;
			}
			if (str[i+1] == 'r') // "\r" -> '\r' (CR)
			{
				unescaped[u++] = '\r';
				++i;
				continue;
			}
			if (str[i+1] == 'n') // "\n" -> '\n' (LF)
			{
				unescaped[u++] = '\n';
				++i;
				continue;
			}
		}
		unescaped[u++] = str[i];
	}
	unescaped[u] = '\0';
	return unescaped;
}

/*
 * Dynamically allocates a twirc_tag_t from the given key and value strings.
 * Returns NULL if memory allocation failed or the given key was NULL or an
 * empty string, otherwise a pointer to the created tag. If the given value 
 * was NULL, it will be set to an empty string. Both key and value are also 
 * dynamically allocated and need to be free'd by the caller at some point.
 */
static twirc_tag_t*
libtwirc_create_tag(const char *key, const char *val)
{
	// Key can't be NULL or empty
	if (key == NULL || strlen(key) == 0)
	{
		return NULL;
	}
	
	// Allocate memory
	twirc_tag_t *tag = malloc(sizeof(twirc_tag_t));

	// Check if allocation worked
	if (tag == NULL) {
		return NULL;
	}

	// Set key and value; if value was NULL, set it to empty string
	tag->key   = strdup(key);
	tag->value = val == NULL ? strdup("") : libtwirc_unescape(val);

	return tag;
}

static void
libtwirc_free_tag(twirc_tag_t *tag)
{
	free(tag->key);
	free(tag->value);
}

static void
libtwirc_free_tags(twirc_tag_t **tags)
{
	if (tags == NULL)
	{
		return;
	}
	for (int i = 0; tags[i] != NULL; ++i)
	{
		libtwirc_free_tag(tags[i]);
		free(tags[i]);
		tags[i] = NULL;
	}
}

static void
libtwirc_free_params(char **params)
{
	if (params == NULL)
	{
		return;
	}
	for (int i = 0; params[i] != NULL; ++i)
	{
		free(params[i]);
		params[i] = NULL;
	}
	//free(params);
}

/*
 * Extracts the nickname from an IRC message's prefix, if any. Done this way:
 * Searches prefix for an exclamation mark ('!'). If there is one, everything 
 * before it will be returned as a pointer to an allocated string (malloc), so
 * the caller has to free() it at some point. If there is no exclamation mark 
 * in prefix or prefix is NULL or we're out of memory, NULL will be returned.
 */
static char*
libtwirc_parse_nick(const char *prefix)
{
	// Nothing to do if nothing has been handed in
	if (prefix == NULL)
	{
		return NULL;
	}
	
	// Search for an exclamation mark in prefix
	char *sep = strstr(prefix, "!");
	if (sep == NULL)
	{
		return NULL;
	}
	
	// Return the nick as malloc'd string
	size_t len = sep - prefix;
	return strndup(prefix, len);
}

/*
 * Extracts tags from the beginning of an IRC message, if any, and returns them
 * as a pointer to a dynamically allocated array of twirc_tag structs, where 
 * each struct contains two members, key and value, representing the key and 
 * value of a tag, respectively. The value member of a tag can be empty string 
 * for key-only tags. The last element of the array will be a NULL pointer, so 
 * you can loop over all tags until you hit NULL. The number of extracted tags
 * is returned in len. If no tags have been found at the beginning of msg, tags
 * will be NULL, len will be 0 and this function will return a pointer to msg.
 * Otherwise, a pointer to the part of msg after the tags will be returned. 
 *
 * https://ircv3.net/specs/core/message-tags-3.2.html
 */
static const char*
libtwirc_parse_tags(const char *msg, twirc_tag_t ***tags, size_t *len)
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
	*tags = malloc(num_tags * sizeof(twirc_tag_t*));

	char *tag = NULL;
	int i;
	for (i = 0; (tag = strtok(i == 0 ? tag_str : NULL, ";")) != NULL; ++i)
	{
		// Make sure we have enough space; last element has to be NULL
		if (i >= num_tags - 1)
		{
			size_t add = (size_t) (num_tags / 2);
			num_tags += add;
			*tags = realloc(*tags, num_tags * sizeof(twirc_tag_t*));
		}

		char *eq = strstr(tag, "=");

		// It's a key-only tag, like "foo" (never seen that on Twitch)
		// Hence, we didn't find a '=' at all 
		if (eq == NULL)
		{
			// TODO we should check for libtwirc_create_tag()
			// 	returning NULL and act accordingly
			(*tags)[i] = libtwirc_create_tag(tag, NULL);
		}
		// It's either a key-only tag with a trailing '=' ("foo=")
		// or a tag with key-value pair, like "foo=bar"
		else
		{
			// Turn the '=' into '\0' to separate key and value
			eq[0] = '\0'; // Turn the '=' into '\0'
			
			// TODO we should check for libtwirc_create_tag()
			// 	returning NULL and act accordingly
			(*tags)[i] = libtwirc_create_tag(tag, eq+1);
		}

		//fprintf(stderr, ">>> TAG %d: %s = %s\n", i, (*tags)[i]->key, (*tags)[i]->value);
	}

	// Set the number of tags found
	*len = i;

	free(tag_str);
	
	// Trim this down to the exact right size
	if (i < num_tags - 1)
	{
		*tags = realloc(*tags, (i + 1) * sizeof(twirc_tag_t*));
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
static const char*
libtwirc_parse_prefix(const char *msg, char **prefix)
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

static const char*
libtwirc_parse_command(const char *msg, char **cmd)
{
	// Find the next space (the end of the cmd string withing msg)
	char *next = strstr(msg, " ");

	// Duplicate the string from start to next space or end of msg
	*cmd = strndup(msg, next == NULL ? strlen(msg) : next - msg);
	
	// Return NULL if cmd was the last bit of msg, or a pointer to
	// the remaining part (the parameters)
	return next == NULL ? NULL : next + 1;
}

static const char*
libtwirc_parse_params(const char *msg, char ***params, size_t *len, int *t_idx)
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

	// Trim this down to the exact amount of memory we need
	if (num_tokens < num_params - 1)
	{
		*params = realloc(*params, (num_tokens + 1) * sizeof(char*));
	}
	
	// Make sure the last element is a NULL ptr
	(*params)[num_tokens] = NULL;

	// We've reached the end of msg, so we'll return NULL
	return NULL;
}

/*
 * Checks if the event is a CTCP event. If so, strips the CTCP markers (0x01)
 * as well as the CTCP command from the trailing parameter and fills the ctcp
 * member of evt with the CTCP command instead. If it isn't a CTCP command, 
 * this function does nothing.
 * Returns 0 on success, -1 if an error occured (not enough memory).
 */
static int
libtwirc_parse_ctcp(twirc_event_t *evt)
{
	// Can't be CTCP if we don't even have enough parameters
	if (evt->num_params <= evt->trailing)
	{
		return 0;
	}
	
	// For convenience, get a ptr to the trailing parameter
	char *trailing = evt->params[evt->trailing];
	
	// First char not 0x01? Not CTCP!
	if (trailing[0] != 0x01)
	{
		return 0;
	}
	
	// Last char not 0x01? Not CTCP!
	int last = strlen(trailing) - 1;
	if (trailing[last] != 0x01)
	{
		return 0;
	}

	// Find the first space within the trailing parameter
	char *space = strstr(trailing, " ");
	if (space == NULL) { return -1; }

	// Copy the CTCP command
	evt->ctcp = strndup(trailing + 1, space - (trailing + 1));
	if (evt->ctcp == NULL) { return -1; }
	
	// Strip 0x01
	char *message = strndup(space + 1, (trailing + last) - (space + 1));
	if (message == NULL) { return -1; }

	// Free the old trailing parameter, swap in the stripped one
	free(evt->params[evt->trailing]);
	evt->params[evt->trailing] = message;

	return 0;
}

static void
libtwirc_dispatch_out(twirc_state_t *s, twirc_event_t *evt)
{
	libtwirc_on_outbound(s, evt);
	s->cbs.outbound(s, evt);
}

/*
 * Dispatches the internal and external event handler / callback functions
 * for the given event, based on the command field of evt. Does not handle
 * CTCP events - call libtwirc_dispatch_ctcp() for those instead.
 */
static void
libtwirc_dispatch_evt(twirc_state_t *s, twirc_event_t *evt)
{
	// TODO try ordering these by "probably usually most frequent", so that
	// we waste as little CPU cycles as possible on strcmp() here!
	
	if (strcmp(evt->command, "PRIVMSG") == 0)
	{
		libtwirc_on_privmsg(s, evt);
		s->cbs.privmsg(s, evt);
		return;
	}
	if (strcmp(evt->command, "JOIN") == 0)
	{
		libtwirc_on_join(s, evt);
		s->cbs.join(s, evt);
		return;
	}
	if (strcmp(evt->command, "CLEARCHAT") == 0)
	{
		libtwirc_on_clearchat(s, evt);
		s->cbs.clearchat(s, evt);
		return;
	}
	if (strcmp(evt->command, "CLEARMSG") == 0)
	{		
		libtwirc_on_clearmsg(s, evt);
		s->cbs.clearmsg(s, evt);
		return;
	}
	if (strcmp(evt->command, "NOTICE") == 0)
	{	
		libtwirc_on_notice(s, evt);
		s->cbs.notice(s, evt);
		return;
	}
	if (strcmp(evt->command, "ROOMSTATE") == 0)
	{		
		libtwirc_on_roomstate(s, evt);
		s->cbs.roomstate(s, evt);
		return;
	}
	if (strcmp(evt->command, "USERSTATE") == 0)
	{		
		libtwirc_on_userstate(s, evt);
		s->cbs.userstate(s, evt);
		return;
	}
	if (strcmp(evt->command, "USERNOTICE") == 0)
	{		
		libtwirc_on_usernotice(s, evt);
		s->cbs.usernotice(s, evt);
		return;
	}
	if (strcmp(evt->command, "WHISPER") == 0)
	{
		libtwirc_on_whisper(s, evt);
		s->cbs.whisper(s, evt);
		return;
	}
	if (strcmp(evt->command, "PART") == 0)
	{
		libtwirc_on_part(s, evt);
		s->cbs.join(s, evt);
		return;
	}
	if (strcmp(evt->command, "PING") == 0)
	{
		libtwirc_on_ping(s, evt);
		s->cbs.ping(s, evt);
		return;
	}
	if (strcmp(evt->command, "MODE") == 0)
	{
		libtwirc_on_mode(s, evt);
		s->cbs.mode(s, evt);
		return;
	}
	if (strcmp(evt->command, "353") == 0 ||
	    strcmp(evt->command, "366") == 0)
	{
		libtwirc_on_names(s, evt);
		s->cbs.names(s, evt);
		return;
	}
	if (strcmp(evt->command, "HOSTTARGET") == 0)
	{
		libtwirc_on_hosttarget(s, evt);
		s->cbs.hosttarget(s, evt);		
		return;
	}
	if (strcmp(evt->command, "CAP") == 0 &&
	    strcmp(evt->params[0], "*") == 0)
	{
		libtwirc_on_capack(s, evt);
		s->cbs.capack(s, evt);
		return;
	}
	if (strcmp(evt->command, "001") == 0)
	{
		libtwirc_on_welcome(s, evt);
		s->cbs.welcome(s, evt);
		return;
	}
	if (strcmp(evt->command, "GLOBALUSERSTATE") == 0)
	{ 
		libtwirc_on_globaluserstate(s, evt);
		s->cbs.globaluserstate(s, evt);
		return;
	}
	if (strcmp(evt->command, "421") == 0)
	{
		libtwirc_on_invalidcmd(s, evt);
		s->cbs.invalidcmd(s, evt);
		return;
	}
	if (strcmp(evt->command, "RECONNECT") == 0)
	{
		libtwirc_on_reconnect(s, evt);
		s->cbs.reconnect(s, evt);
		return;
	}
	
	// Some unaccounted-for event occured
	libtwirc_on_other(s, evt);
	s->cbs.other(s, evt);
}

/*
 * Dispatches the internal and external event handler / callback functions
 * for the given CTCP event, based on the ctcp field of evt. Does not handle
 * regular events - call libtwirc_dispatch_evt() for those instead.
 */
static void
libtwirc_dispatch_ctcp(twirc_state_t *s, twirc_event_t *evt)
{
	if (strcmp(evt->ctcp, "ACTION") == 0)
	{
		libtwirc_on_action(s, evt);
		s->cbs.action(s, evt);
		return;
	}
	
	// Some unaccounted-for event occured
	libtwirc_on_other(s, evt);
	s->cbs.other(s, evt);
}

/*
 * Takes a raw IRC message and parses all the relevant information into a 
 * twirc_event struct, then calls upon the functions responsible for the 
 * dispatching of the event to internal and external callback functions.
 * Returns 0 on success, -1 if an out of memory error occured during the
 * parsing/handling of a CTCP event.
 */
static int
libtwirc_process_msg(twirc_state_t *s, const char *msg, int outbound)
{
	//fprintf(stderr, "> %s (%zu)\n", msg, strlen(msg));

	int err = 0;
	twirc_event_t evt = { 0 };

	evt.raw = strdup(msg);

	// Extract the tags, if any
	msg = libtwirc_parse_tags(msg, &(evt.tags), &(evt.num_tags));

	// Extract the prefix, if any
	msg = libtwirc_parse_prefix(msg, &(evt.prefix));

	// Extract the command, always
	msg = libtwirc_parse_command(msg, &(evt.command));

	// Extract the parameters, if any
	msg = libtwirc_parse_params(msg, &(evt.params), &(evt.num_params), &(evt.trailing));

	// Check for CTCP and possibly modify the event accordingly
	err = libtwirc_parse_ctcp(&evt);

	// Extract the nick from the prefix, maybe
	evt.origin = libtwirc_parse_nick(evt.prefix);
	
	if (outbound)
	{
		libtwirc_dispatch_out(s, &evt);
	}
	else if (evt.ctcp)
	{
		libtwirc_dispatch_ctcp(s, &evt);
	}
	else
	{
		libtwirc_dispatch_evt(s, &evt);
	}

	// Free event
	// TODO: make all of this into a function? libtwirc_free_event()
	libtwirc_free_params(evt.params);
	free(evt.params);
	evt.params = NULL;
	libtwirc_free_tags(evt.tags);
	free(evt.tags);
	evt.tags = NULL;
	free(evt.raw);
	free(evt.prefix);
	free(evt.origin);
	free(evt.target);
	free(evt.command);
	free(evt.ctcp);

	return err;
}

/*
 * Process the raw IRC data stored in `buf`, which has a size of len bytes.
 * Incomplete commands will be buffered in state->buffer, complete commands 
 * will be processed right away. Returns 0 on success, -1 if out of memory.
 */
int
libtwirc_process_data(twirc_state_t *s, const char *buf, size_t len)
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
	if (chunk == NULL) { return libtwirc_oom(s); }
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
		strcat(s->buffer, chunk);
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

	while (libtwirc_shift_token(msg, s->buffer, "\r\n") > 0)
	{
		// Process the message and check if we ran out of memory doing so
		if (libtwirc_process_msg(s, msg, 0) == -1)
		{
			return -1;
		}
	}
	
	return 0;
}

/*
 * Handles the epoll event epev.
 * Returns 0 on success, -1 if the connection has been interrupted or
 * not enough memory was available to process the incoming data.
 */
static int
libtwirc_handle_event(twirc_state_t *s, struct epoll_event *epev)
{
	// We've got data coming in
	if(epev->events & EPOLLIN)
	{
		char buf[TWIRC_BUFFER_SIZE];
		int bytes_received = 0;
		
		// Fetch and process all available data from the socket
		while ((bytes_received = libtwirc_recv(s, buf, TWIRC_BUFFER_SIZE)) > 0)
		{
			// Process the data and check if we ran out of memory doing so
			if (libtwirc_process_data(s, buf, bytes_received) == -1)
			{
				s->error = TWIRC_ERR_OUT_OF_MEMORY;
				return -1;
			}
		}
		
		// If twirc_recv() returned -1, the connection is probably down,
		// either way, we  have a serious issue and should stop running!
		if (bytes_received == -1)
		{
			s->error = TWIRC_ERR_SOCKET_RECV;
			
			// We were connected but now seem to be disconnected?
			if (twirc_is_connected(s) && tcpsock_status(s->socket_fd) == -1)
			{
				// If so, call the disconnect event handlers
				libtwirc_on_disconnect(s);
				s->cbs.disconnect(s, NULL);
			}
			return -1;
		}
	}
	
	// We're ready to send data
	if (epev->events & EPOLLOUT)
	{
		// If we weren't connected yet, we seem to be now!
		if (s->status & TWIRC_STATUS_CONNECTING)
		{		
			// The internal connect event handler will initiate the
			// request of capabilities as well as the login process
			libtwirc_on_connect(s);
			s->cbs.connect(s, NULL);
		}
	}
	
	// Server closed the connection
	if (epev->events & EPOLLRDHUP)
	{
		s->error = TWIRC_ERR_CONN_CLOSED;
		libtwirc_on_disconnect(s);
		s->cbs.disconnect(s, NULL);
		return -1;
	}
	
	// Unexpected hangup on socket 
	if (epev->events & EPOLLHUP) // fires even if not added explicitly
	{
		s->error = TWIRC_ERR_CONN_HANGUP;
		libtwirc_on_disconnect(s);
		s->cbs.disconnect(s, NULL);
		return -1;
	}

	// Socket error
	if (epev->events & EPOLLERR) // fires even if not added explicitly
	{
		s->error = TWIRC_ERR_CONN_SOCKET;
		libtwirc_on_disconnect(s);
		s->cbs.disconnect(s, NULL);
		return -1;
	}
	
	// Handled everything and no disconnect/error occurred
	return 0;
}

/*
 * Sends data to the IRC server, using the state's socket.
 * On success, returns the number of bytes sent.
 * On error, -1 is returned and errno is set appropriately.
 */
static int
libtwirc_send(twirc_state_t *s, const char *msg)
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

	// Actually send the message
	int ret = tcpsock_send(s->socket_fd, buf, buf_len);
	
	// Dispatch the outgoing event
	libtwirc_process_msg(s, msg, 1);

	free(buf);
	return ret;
}

/*
 * Reads data from the socket and copies it into the provided buffer `buf`.
 * Returns the number of bytes read or 0 if there was no more data to read.
 * If an error occured, -1 will be returned (check errno); this usually means
 * the connection has been lost or some error has occurred on the socket.
 */
static int
libtwirc_recv(twirc_state_t *s, char *buf, size_t len)
{
	// Receive data
	ssize_t res_len;
	res_len = tcpsock_receive(s->socket_fd, buf, len - 1);

	// Check if tcpsno_receive() reported an error
	if (res_len == -1)
	{
		if (errno == EAGAIN || errno == EWOULDBLOCK)
		{
			// Simply no more data to read right now - all good
			return 0;
		}
		// Every other error, however, indicates some serious problem
		return -1;
	}
	
	// Make sure that the received data is null terminated
	buf[res_len] = '\0';

	// Return the number of bytes received
	return res_len;
}

/*
 * Ininitates an anonymous connection with the given server.
 * The username will be `justinfan` plus a randomly generated numeric suffix.
 * Returns 0 if the connection process has started and is now in progress,
 * -1 if the connection attempt failed (check the state's error and errno).
 */
int
twirc_connect_anon(twirc_state_t *s, const char *host, const char *port)
{
	int r = rand() % (10 * TWIRC_USER_ANON_MAX_DIGITS);
	size_t len = (strlen(TWIRC_USER_ANON) + TWIRC_USER_ANON_MAX_DIGITS + 1); 

	char *anon = malloc(len * sizeof(char));
	if (anon == NULL) { return libtwirc_oom(s); }
	snprintf(anon, len, "%s%d", TWIRC_USER_ANON, r);

	int res = twirc_connect(s, host, port, anon, "null");
	free(anon);
	return res;
}

/*
 * Initiates a connection with the given server using the given credentials.
 * Returns 0 if the connection process has started and is now in progress, 
 * -1 if the connection attempt failed (check the state's error and errno).
 */
int
twirc_connect(twirc_state_t *s, const char *host, const char *port, const char *nick, const char *pass)
{
	// Create socket
	s->socket_fd = tcpsock_create(s->ip_type, TCPSOCK_NONBLOCK);
	if (s->socket_fd < 0)
	{
		s->error = TWIRC_ERR_SOCKET_CREATE;
		return -1;
	}

	// Create epoll instance 
	s->epfd = epoll_create(1);
	if (s->epfd < 0)
	{
		s->error = TWIRC_ERR_EPOLL_CREATE;
		return -1;
	}

	// Set up the epoll instance
	struct epoll_event eev = { 0 };
	eev.data.ptr = s;
	eev.events = EPOLLRDHUP | EPOLLOUT | EPOLLIN | EPOLLET;
	int epctl_result = epoll_ctl(s->epfd, EPOLL_CTL_ADD, s->socket_fd, &eev);
	
	if (epctl_result)
	{
		// Socket could not be registered for IO
		s->error = TWIRC_ERR_EPOLL_CTL;
		return -1;
	}

	// Properly initialize the login struct and copy the login data into it
	s->login.host = strdup(host);
	s->login.port = strdup(port);
	s->login.nick = strdup(nick);
	s->login.pass = strdup(pass);

	// Connect the socket (and handle a possible connection error)
	if (tcpsock_connect(s->socket_fd, s->ip_type, host, port) == -1)
	{
		s->error = TWIRC_ERR_SOCKET_CONNECT;
		return -1;
	}

	// We are in the process of connecting!
	s->status = TWIRC_STATUS_CONNECTING;
	return 0;
}

/*
 * Sends the QUIT command to the server, then terminates the connection and 
 * calls both the internal as well as external disconnect event handlers.
 * Returns 0 on success, -1 if the socket could not be closed (see errno).
 */ 
int
twirc_disconnect(twirc_state_t *s)
{
	// Say bye-bye to the IRC server
	twirc_cmd_quit(s);
	
	// Close the socket and return if that worked
	return tcpsock_close(s->socket_fd);

	// Note that we are NOT calling the disconnect event handlers from
	// here; this is on purpose! We only want to call these from within
	// libtwirc_handle_event() and, in one case, twirc_tick(), to avoid
	// situations where they might be raised twice. Remember: closing 
	// the socket here might lead to an epoll event that 'naturally' 
	// leads to us calling the disconnect handlers anyway. If that does
	// not happen, well, then so be it. If the user called upon this
	// function, they should expect the connection to be down shortly
	// after. Of course, this would leave us in an inconsistent state,
	// as s->state would report that we're still connected, but oh well.
	// TODO test/investigate further if this could be an issue or not.
}

/*
 * Sets all callback members to the dummy callback
 */
void
twirc_init_callbacks(twirc_callbacks_t *cbs)
{
	// TODO figure out if there is a more elegant and dynamic way...
	cbs->connect         = libtwirc_on_null;
	cbs->welcome         = libtwirc_on_null;
	cbs->globaluserstate = libtwirc_on_null;
	cbs->capack          = libtwirc_on_null;
	cbs->ping            = libtwirc_on_null;
	cbs->join            = libtwirc_on_null;
	cbs->part            = libtwirc_on_null;
	cbs->mode            = libtwirc_on_null;
	cbs->names           = libtwirc_on_null;
	cbs->privmsg         = libtwirc_on_null;
	cbs->whisper         = libtwirc_on_null;
	cbs->action          = libtwirc_on_null;
	cbs->notice          = libtwirc_on_null;
	cbs->roomstate       = libtwirc_on_null;
	cbs->usernotice      = libtwirc_on_null;
	cbs->userstate       = libtwirc_on_null;
	cbs->clearchat       = libtwirc_on_null;
	cbs->clearmsg        = libtwirc_on_null;
	cbs->hosttarget      = libtwirc_on_null;
	cbs->reconnect       = libtwirc_on_null;
	cbs->disconnect      = libtwirc_on_null;
	cbs->invalidcmd      = libtwirc_on_null;
	cbs->other           = libtwirc_on_null;
	cbs->outbound        = libtwirc_on_null;
}

/*
 * Returns a pointer to the state's twirc_callbacks structure. 
 * This allows the user to set select callbacks to their handler functions.
 * Under no circumstances should the user set any callback to NULL, as this
 * will eventually lead to a segmentation fault, as libtwirc relies on the
 * fact that every callback that wasn't assigned to by the user is assigned
 * to an internal dummy (null) event handler.
 */
twirc_callbacks_t*
twirc_get_callbacks(twirc_state_t *s)
{
	return &s->cbs;
}

/*
 * Returns a pointer to a twirc_state struct, which represents the state of
 * the connection to the server, the state of the user, holds the login data,
 * all callback function pointers for event handling and much more. Returns
 * a NULL pointer if any errors occur during initialization.
 */
twirc_state_t*
twirc_init()
{
	// Seed the random number generator
	srand(time(NULL));

	// Init state struct
	twirc_state_t *s = malloc(sizeof(twirc_state_t));
	if (s == NULL) { return NULL; } 
	memset(s, 0, sizeof(twirc_state_t));

	// Set some defaults / initial values
	s->status    = TWIRC_STATUS_DISCONNECTED;
	s->ip_type   = TWIRC_IPV4;
	s->socket_fd = -1;
	s->error     = 0;
	
	// Initialize the buffer - it will be twice the message size so it can
	// easily hold an incomplete message in addition to a complete one
	s->buffer = malloc(2 * TWIRC_MESSAGE_SIZE * sizeof(char));
	if (s->buffer == NULL) { return NULL; } 
	s->buffer[0] = '\0';

	// Make sure the structs within state are zero-initialized
	memset(&s->login, 0, sizeof(twirc_login_t));
	memset(&s->cbs,   0, sizeof(twirc_callbacks_t));

	// Set all callbacks to the dummy callback
	twirc_init_callbacks(&s->cbs);

	// All done
	return s;
}

/*
 * Frees the twirc_state and all of its members.
 */
void
twirc_free(twirc_state_t *s)
{
	close(s->epfd);
	libtwirc_free_callbacks(s);
	libtwirc_free_login(s);
	free(s->buffer);
	free(s);
	s = NULL;
}

/*
 * Schwarzeneggers the connection with the server and frees the twirc_state.
 * Hence, do not call twirc_free() after this function, it's already done.
 */
void
twirc_kill(twirc_state_t *s)
{
	if (twirc_is_connected(s))
	{
		twirc_disconnect(s);
	}
	twirc_free(s);
}

/*
 * Waits timeout milliseconds for events to happen on the IRC connection.
 * Returns 0 if all events have been handled and -1 if an error has been 
 * encountered and/or the connection has been lost. To determine whether the 
 * connection has been lost, use twirc_is_connected(). If the connection is 
 * still up, for example, if we were interrupted by a SIGSTOP signal, then it
 * is up to the user to decide whether they want to disconnect now or keep the
 * connection alive. Remember, however, that messages will keep piling up in 
 * the kernel; if your program is handling very busy channels, you might not 
 * want to stay connected without handling those messages for too long. 
 */
int
twirc_tick(twirc_state_t *s, int timeout)
{
	struct epoll_event epev;
	
	// epoll_wait()/epoll_pwait() will return -1 if a signal is caught.
	// User code might catch "harmless" signals, like SIGWINCH, that are
	// ignored by default. This would then cause epoll_wait() to return
	// with -1, hence our main loop to come to a halt. This is not what
	// a user would expect; we should only come to a halt on "serious"
	// signals that would cause program termination/halt by default.
	// In order to achieve this, we tell epoll_pwait() to block all of
	// the signals that are ignored by default. For a list of signals:
	// https://en.wikipedia.org/wiki/Signal_(IPC)
	
	sigset_t sigset;
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGCHLD);  // default: ignore
	sigaddset(&sigset, SIGCONT);  // default: continue execution
	sigaddset(&sigset, SIGURG);   // default: ignore
	sigaddset(&sigset, SIGWINCH); // default: ignore

	int num_events = epoll_pwait(s->epfd, &epev, 1, timeout, &sigset);

	// An error has occured
	if (num_events == -1)
	{
		// The exact reason why epoll_wait failed can be queried through
		// errno; the possibilities include wrong/faulty parameters and,
		// more interesting, that a signal has interrupted epoll_wait().
		// Wrong parameters will either happen on the very first call or
		// not at all, but a signal could come in anytime. Either way, 
		// epoll_wait() failing doesn't necessarily mean that we lost 
		// the connection with the server. Some signals, like SIGSTOP 
		// can mean that we're simply supposed to stop execution until 
		// a SIGCONT is received. Hence, it seems like a good idea to 
		// leave it up to the user what to do, which means that we are
		// not going to quit/disconnect from IRC; we're simply going to
		// return -1 to indicate an issue. The user can then check the 
		// connection status and decide if they want to explicitly end 
		// the connection or keep it alive. One exception: if we can 
		// actually determine, right here, that the connection seems to
		// be down, then we'll set off the disconnect event handlers.
		// For this, we'll use tcpsock_status().

		// Set the error accordingly:
		//  - TWIRC_ERR_EPOLL_SIG  if epoll_pwait() caught a signal
		//  - TWIRC_ERR_EPOLL_WAIT for any other error in epoll_wait()
		s->error = errno == EINTR ? TWIRC_ERR_EPOLL_SIG : TWIRC_ERR_EPOLL_WAIT;
		
		// Were we connected previously but now seem to be disconnected?
		if (twirc_is_connected(s) && tcpsock_status(s->socket_fd) == -1)
		{
			// ...if so, call the disconnect event handlers
			libtwirc_on_disconnect(s);
			s->cbs.disconnect(s, NULL);
		}
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
 * Runs an endless loop that waits for and processes IRC events until either
 * the connection has been closed or some serious error has occured that caused
 * twirc_tick() to return -1, at which point the loop ends. Returns 0 if the 
 * connection to the IRC server has been lost or 1 if the connection is still 
 * up and the loop has ended for some other reason (check the state's error 
 * field and possibly errno for additional details).
 */
int
twirc_loop(twirc_state_t *s)
{
	// TODO we should probably put some connection time-out code in place
	// so that we stop running after the connection attempt has been going 
	// on for so-and-so long. Or shall we leave that up to the user code?

	while (twirc_tick(s, -1) == 0)
	{
		// Nothing to do here, actually. :-)
	}
	return twirc_is_connected(s);
}


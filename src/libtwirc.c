#include <stdio.h>      // NULL, fprintf(), perror()
#include <stdlib.h>     // NULL, EXIT_FAILURE, EXIT_SUCCESS
#include <errno.h>      // errno
#include <netdb.h>      // getaddrinfo()
#include <unistd.h>     // close(), fcntl()
#include <string.h>     // strlen(), strerror()
#include <fcntl.h>      // fcntl()
#include <ctype.h>      // isspace()
#include <sys/types.h>  // ssize_t
#include <sys/socket.h> // socket(), connect(), send(), recv()
#include <sys/epoll.h>  // epoll_create(), epoll_ctl(), epoll_wait()
#include "tcpsnob/tcpsnob.h"
#include "libtwirc.h"

/*
 * Initiates a connection with the given server using the given credentials.
 * Returns 0 if the connection process has started and is now in progress, 
 * -1 if the connection attempt failed (check the state's error and errno).
 */
int twirc_connect(struct twirc_state *s, const char *host, const char *port, const char *pass, const char *nick)
{
	// Create socket
	s->socket_fd = tcpsnob_create(s->ip_type);
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
	if (tcpsnob_connect(s->socket_fd, s->ip_type, host, port) == -1)
	{
		s->error = TWIRC_ERR_SOCKET_CONNECT;
		return -1;
	}

	// We are connected!
	s->status = TWIRC_STATUS_CONNECTING;
	return 0;
}

/*
 * Sends data to the IRC server, using the state's socket.
 * On success, returns the number of bytes sent.
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

	if (strstr(buf, "PASS") == NULL)
	{
		fprintf(stderr, "< %s", buf);
	}

	int ret = tcpsnob_send(state->socket_fd, buf, buf_len);
	free(buf);

	return ret;
}

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
	res_len = tcpsnob_receive(state->socket_fd, buf, len - 1);

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
	
	// Make super-sure that the data received is null terminated
	buf[res_len] = '\0';

	// Return the number of bytes received
	return res_len;
}

/*
 * Returns 1 if state is connected to Twitch IRC, otherwise 0.
 */
int twirc_is_connected(const struct twirc_state *state)
{
	return state->status & TWIRC_STATUS_CONNECTED ? 1 : 0;
}

/*
 * Returns 1 if state is authenticated (logged in), otherwise 0.
 */
int twirc_is_logged_in(const struct twirc_state *state)
{
	return state->status & TWIRC_STATUS_AUTHENTICATED ? 1 : 0;
}

/*
 * Sends the PASS command to the server, with the pass appended as parameter.
 * This is the first part of the authentication process (next part is NICK).
 * Returns 0 if the command was sent successfully, -1 on error.
 */
int twirc_cmd_pass(struct twirc_state *state, const char *pass)
{
	char msg[TWIRC_BUFFER_SIZE];
	snprintf(msg, TWIRC_BUFFER_SIZE, "PASS %s", pass);
	return twirc_send(state, msg);
}

/*
 * Sends the NICK command to the server, with the nick appended as parameter.
 * This is the second part of the authentication process (first is PASS).
 * Returns 0 if the command was sent successfully, -1 on error.
 */
int twirc_cmd_nick(struct twirc_state *state, const char *nick)
{
	char msg[TWIRC_BUFFER_SIZE];
	snprintf(msg, TWIRC_BUFFER_SIZE, "NICK %s", nick);
	return twirc_send(state, msg);
}

/*
 * Request to join the specified channel.
 * Returns 0 if the command was sent successfully, -1 on error.
 */
int twirc_cmd_join(struct twirc_state *state, const char *chan)
{
	char msg[TWIRC_BUFFER_SIZE];
	snprintf(msg, TWIRC_BUFFER_SIZE, "JOIN %s", chan);
	return twirc_send(state, msg);
}

/*
 * Leave (part) the specified channel.
 * Returns 0 if the command was sent successfully, -1 on error.
 */
int twirc_cmd_part(struct twirc_state *state, const char *chan)
{
	char msg[TWIRC_BUFFER_SIZE];
	snprintf(msg, TWIRC_BUFFER_SIZE, "PART %s", chan);
	return twirc_send(state, msg);
}

/*
 * Send a message (privmsg) to the specified channel.
 * Returns 0 if the command was sent successfully, -1 on error.
 */
int twirc_cmd_privmsg(struct twirc_state *state, const char *chan, const char *msg)
{
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :%s", chan, msg);
	return twirc_send(state, privmsg);
}

/*
 * Send a CTCP ACTION message (aka "/me") to the specified channel.
 * Returns 0 if the command was sent successfully, -1 on error.
 */
int twirc_cmd_action(struct twirc_state *state, const char *chan, const char *msg)
{
	// "PRIVMSG #<chan> :\x01ACTION <msg>\x01"
	char action[TWIRC_MESSAGE_SIZE];
	action[0] = '\0';
	snprintf(action, TWIRC_MESSAGE_SIZE, "PRIVMSG %s :%cACTION %s%c",
			chan, '\x01', msg, '\x01');
	return twirc_send(state, action);
}

/*
 * Send a whisper message to the specified user.
 * Returns 0 if the command was sent successfully, -1 on error.
 */
int twirc_cmd_whisper(struct twirc_state *state, const char *nick, const char *msg)
{
	// "PRIVMSG #jtv :/w <user> <msg>"
	char whisper[TWIRC_MESSAGE_SIZE];
	snprintf(whisper, TWIRC_MESSAGE_SIZE, "PRIVMSG %s :/w %s %s", 
			TWIRC_WHISPER_CHANNEL, nick, msg);
	return twirc_send(state, whisper);
}

/*
 * Requests the tags capability from the Twitch server.
 * Returns 0 if the command was sent successfully, -1 on error.
 */
int twirc_cmd_req_tags(struct twirc_state *state)
{
	return twirc_send(state, "CAP REQ :twitch.tv/tags");
}

/*
 * Requests the membership capability from the Twitch server.
 * Returns 0 if the command was sent successfully, -1 on error.
 */
int twirc_cmd_req_membership(struct twirc_state *state)
{
	return twirc_send(state, "CAP REQ :twitch.tv/membership");
}

/*
 * Requests the commands capability from the Twitch server.
 * Returns 0 if the command was sent successfully, -1 on error.
 */
int twirc_cmd_req_commands(struct twirc_state *state)
{
	return twirc_send(state, "CAP REQ :twitch.tv/commands");
}

/*
 * Requests the chatrooms capability from the Twitch server.
 * Returns 0 if the command was sent successfully, -1 on error.
 */
int twirc_cmd_req_chatrooms(struct twirc_state *state)
{
	return twirc_send(state, "CAP REQ :twitch.tv/tags twitch.tv/commands");
}

/*
 * Requests the tags, membership, commands and chatrooms capabilities.
 * Returns 0 if the command was sent successfully, -1 on error.
 */
int twirc_cmd_req_all(struct twirc_state *state)
{
	return twirc_send(state, 
	    "CAP REQ: twitch.tv/tags twitch.tv/commands twitch.tv/membership");
}

/*
 * Sends the PONG command to the IRC server.
 * If param is given, it will be appended. To make Twitch happy (this is not 
 * part of the IRC specification) the param will be prefixed with a colon (":")
 * unless it is prefixed with one already.
 * Returns 0 on success, -1 otherwise.
 */
int twirc_cmd_pong(struct twirc_state *state, const char *param)
{
	// "PONG :tmi.twitch.tv"
	char pong[TWIRC_PONG_SIZE];
	pong[0] = '\0';
	snprintf(pong, TWIRC_PONG_SIZE, "PONG %s%s", 
			param && param[0] == ':' ? "" : ":",
			param ? param : "");
	return twirc_send(state, pong);
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
 * Requests all supported capabilities from the Twitch servers.
 * Returns 0 if the request was sent successfully, -1 on error.
 */
int twirc_capreq(struct twirc_state *s)
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
 * This dummy callback function does absolutely nothing.
 * However, it allows us to make sure that all callbacks are non-NULL, removing 
 * the need to check for NULL everytime before we call them. On the other hand,
 * this does introduce the overhead of a function call instead of a NULL-check.
 * This is probably worse, performance-wise, but as long we don't run into any
 * performance problems, I prefer this as it makes for a much leaner code. 
 * Maybe `static inline` enables the compiler to optimize the overhead.
 */
static inline
void libtwirc_on_null(struct twirc_state *s, struct twirc_event *evt)
{
	// Nothing in here. That's on purpose.
}

/*
 * Invalid IRC Commands
 * If you send an invalid command, you will get a 421 message back:
 *
 * < WHO #<channel>
 * > :tmi.twitch.tv 421 <user> WHO :Unknown command
 */
void libtwirc_on_invalidcmd(struct twirc_state *s, struct twirc_event *evt)
{
	// Don't think we have to do anything here, honestly
}

/*
 * Handler for the "001" command (RPL_WELCOME), which the Twitch servers send
 * on successful login, even when no capabilities have been requested.
 */
void libtwirc_on_welcome(struct twirc_state *s, struct twirc_event *evt)
{
	s->status |= TWIRC_STATUS_AUTHENTICATED;
}

/*
 * On successful login.
 * > @badges=<badges>;color=<color>;display-name=<display-name>;
 *   emote-sets=<emote-sets>;turbo=<turbo>;user-id=<user-id>;user-type=<user-type>
 *    :tmi.twitch.tv GLOBALUSERSTATE
 *
 * badges:       Comma-separated list of chat badges and the version of each badge 
 *               (each in the format <badge>/<version>, such as admin/1). Valid 
 *               badge values: admin, bits, broadcaster, global_mod, moderator, 
 *               subscriber, staff, turbo.
 * color:        Hexadecimal RGB color code. This is empty if it is never set.
 * display-name: The user’s display name, escaped as described in the IRCv3 spec.
 *               This is empty if it is never set.
 * emote-sets:   A comma-separated list of emotes, belonging to one or more emote
 *               sets. This always contains at least 0. Get Chat Emoticons by Set
 *               gets a subset of emoticons.
 * user-id:      The user’s ID.
 */
void libtwirc_on_globaluserstate(struct twirc_state *s, struct twirc_event *evt)
{
	s->status |= TWIRC_STATUS_AUTHENTICATED;
}

void libtwirc_on_capack(struct twirc_state *s, struct twirc_event *evt)
{
	// TODO
}

void libtwirc_on_ping(struct twirc_state *s, struct twirc_event *evt)
{
	twirc_cmd_pong(s, evt->params[0]);
}

/*
 * Join a channel.
 * 
 * > :<user>!<user>@<user>.tmi.twitch.tv JOIN #<channel>
 */
void libtwirc_on_join(struct twirc_state *s, struct twirc_event *evt)
{
	evt->channel = evt->params[0];
}

/*
 * Gain/lose moderator (operator) status in a channel.
 *
 * > :jtv MODE #<channel> +o <user>
 * > :jtv MODE #<channel> -o <user>
 */
void libtwirc_on_mode(struct twirc_state *s, struct twirc_event *evt)
{
	evt->channel = evt->params[0];
}

/*
 * List current chatters in a channel. 
 * If there are more than 1000 chatters in a room, NAMES return only the list 
 * of operator privileges currently in the room.
 *
 * > :<user>.tmi.twitch.tv 353 <user> = #<channel> :<user> <user2> <user3>
 * > :<user>.tmi.twitch.tv 353 <user> = #<channel> :<user4> <user5> ... <userN>
 * > :<user>.tmi.twitch.tv 366 <user> #<channel> :End of /NAMES list
 */
void libtwirc_on_names(struct twirc_state *s, struct twirc_event *evt)
{
	// TODO
}

/*
 * Depart from a channel.
 *
 * > :<user>!<user>@<user>.tmi.twitch.tv PART #<channel>
 */
void libtwirc_on_part(struct twirc_state *s, struct twirc_event *evt)
{
	evt->channel = evt->params[0];
}

/*
 * Temporary or permanent ban on a channel. 
 * > @ban-duration=<ban-duration> :tmi.twitch.tv CLEARCHAT #<channel> :<user>
 *
 * ban-duration: (Optional) Duration of the timeout, in seconds.
 *               If omitted, the ban is permanent.
 * 
 * TODO: Figure out how we can know WHO banned <user>
 *       (as the prefix doesn't seem to contain the nick?)
 */
void libtwirc_on_clearchat(struct twirc_state *s, struct twirc_event *evt)
{
	evt->channel = evt->params[0];
}

/*
 * Single message removal on a channel. This is triggered via 
 * /delete <target-msg-id> on IRC.
 *
 * > @login=<login>;target-msg-id=<target-msg-id> 
 *    :tmi.twitch.tv CLEARMSG #<channel> :<message>
 * 
 * login:         Name of the user who sent the message.
 * message:       The message.
 * target-msg-id: UUID of the message.
 */
void libtwirc_on_clearmsg(struct twirc_state *s, struct twirc_event *evt)
{
	evt->channel = evt->params[0];
}

/*
 * Channel starts or stops host mode.
 *
 * Start:
 * > :tmi.twitch.tv HOSTTARGET #hosting_channel <channel> [<number-of-viewers>]
 *
 * Stop:
 * > :tmi.twitch.tv HOSTTARGET #hosting_channel :- [<number-of-viewers>]
 *
 * number-of-viewers: (Optional) Number of viewers watching the host.
 *
 * Example:
 * > :tmi.twitch.tv HOSTTARGET #domsson :fujioka_twitch -
 */
void libtwirc_on_hosttarget(struct twirc_state *s, struct twirc_event *evt)
{
	// TODO got to figure out the exact syntax:
	//      - Channels without # ?
	//      - number-of-viewers is '-' when not given?
	//      - channel and number-of-viewers are together the trailing param?
	evt->channel = evt->params[0];
}

/*
 * General notices from the server.
 *
 * > @msg-id=<msg id> :tmi.twitch.tv NOTICE #<channel> :<message>
 *
 * message: The message.
 * msg id:  A message ID string. Can be used for i18ln. Valid values: 
 *          see Twitch IRC: msg-id Tags.
 *          https://dev.twitch.tv/docs/irc/msg-id/
 *
 * Example:
 * > @msg-id=host_target_went_offline 
 *    :tmi.twitch.tv NOTICE #domsson 
 *    :joshachu has gone offline. Exiting host mode.
 */
void libtwirc_on_notice(struct twirc_state *s, struct twirc_event *evt)
{
	evt->channel = evt->params[0];
	evt->message = evt->params[evt->trailing];
}

/*
 * CTCP ACTION
 */
void libtwirc_on_action(struct twirc_state *s, struct twirc_event *evt)
{
	evt->channel = evt->params[0];
	evt->message = evt->params[1];
}

/*
 * Rejoin channels after a restart.
 * 
 * Twitch IRC processes occasionally need to be restarted. When this happens,
 * clients that have requested the IRC v3 twitch.tv/commands capability are 
 * issued a RECONNECT. After a short time, the connection is closed. In this 
 * case, reconnect and rejoin channels that were on the connection, as you 
 * would normally.
 */
void libtwirc_on_reconnect(struct twirc_state *s, struct twirc_event *evt)
{
	// Probably nothing to do here
}

/*
 * Send a message to a channel.
 * > @badges=<badges>;color=<color>;display-name=<display-name>;emotes=<emotes>;
 *   id=<id-of-msg>;mod=<mod>;room-id=<room-id>;subscriber=<subscriber>;
 *   tmi-sent-ts=<timestamp>;turbo=<turbo>;user-id=<user-id>;user-type=<user-type>
 *    :<user>!<user>@<user>.tmi.twitch.tv PRIVMSG #<channel> :<message>
 *
 * badges:       Comma-separated list of chat badges and the version of each 
 *               badge (each in the format <badge>/<version>, such as admin/1).
 *               Valid badge values: admin, bits, broadcaster, global_mod, 
 *               moderator, subscriber, staff, turbo.
 * bits:         (Sent only for Bits messages) The amount of cheer/Bits employed
 *               by the user. All instances of these regular expressions:
 *               /(^\|\s)<emote-name>\d+(\s\|$)/
 *               (where <emote-name> is an emote name returned by the 
 *               Get Cheermotes endpoint), should be replaced with the 
 *               appropriate emote:
 *               static-cdn.jtvnw.net/bits/<theme>/<type>/<color>/<size>
 *               - theme: light or dark
 *               - type:  animated or static
 *               - color: red for 10000+ Bits, blue for 5000-9999, green for 
 *                 1000-4999, purple for 100-999, gray for 1-99
 *               - size:  A digit between 1 and 4
 * color:        Hexadecimal RGB color code. This is empty if it is never set.
 * display-name: The user’s display name, escaped as described in the IRCv3 spec.
 *               This is empty if it is never set.
 * emotes:       Information to replace text in the message with emote images.
 *               This can be empty. Syntax:
 *               <eid>:<f>-<l>,<f>-<l>/<eid>:<f>-<l>...
 *               - eid: The number to use in this URL:
 *                 http://static-cdn.jtvnw.net/emoticons/v1/:<eid>/:<size>
 *                 (size is 1.0, 2.0 or 3.0.)
 *               - f/l: Character indexes. \001ACTION does not count.
 *                 Indexing starts from the first character that is part of the
 *                 user’s actual message.
 * id:           A unique ID for the message.
 * message:      The message.
 * mod:          1 if the user has a moderator badge; otherwise, 0.
 * room-id:      The channel ID.
 * subscriber:   (Deprecated, use badges instead) 1 if the user has a
 *               subscriber badge; otherwise, 0.
 * tmi-sent-ts:  Timestamp when the server received the message.
 * turbo:        (Deprecated, use badges instead) 1 if the user has a Turbo 
 *               badge; otherwise, 0.
 * user-id:      The user’s ID.
 * user-type:    (Deprecated, use badges instead) The user’s type. Valid values: 
 *               mod, global_mod, admin, staff. If the broadcaster is not any of
 *               these user types, this field is left empty.
 */
void libtwirc_on_privmsg(struct twirc_state *s, struct twirc_event *evt)
{
	evt->channel = evt->params[0];
	evt->message = evt->params[evt->trailing];
}

/*
 * When a user joins a channel or a room setting is changed.
 * For a join, the message contains all chat-room settings. For changes, only the relevant tag is sent.
 *
 * > @broadcaster-lang=<broadcaster-lang>;emote-only=<emote-only>;
 *   followers-only=<followers-only>;r9k=<r9k>;slow=<slow>;subs-only=<subs-only>
 *    :tmi.twitch.tv ROOMSTATE #<channel>
 *
 * broadcaster-lang: The chat language when broadcaster language mode is
 *                   enabled; otherwise, empty. Examples: en (English), 
 *                   fi (Finnish), es-MX (Mexican variant of Spanish).
 * emote-only:       Emote-only mode. If enabled, only emotes are allowed in
 *                   chat. Valid values: 0 (disabled) or 1 (enabled).
 * followers-only:   Followers-only mode. If enabled, controls which followers 
 *                   can chat. Valid values: -1 (disabled), 0 (all followers 
 *                   can chat), or a non-negative integer (only users following
 *                   for at least the specified number of minutes can chat).
 * r9k:              R9K mode. If enabled, messages with more than 9 characters
 *                   must be unique. Valid values: 0 (disabled) or 1 (enabled).
 * slow:             The number of seconds chatters without moderator 
 *                   privileges must wait between sending messages.
 * subs-only:        Subscribers-only mode. If enabled, only subscribers and 
 *                   moderators can chat. Valid values: 0 (disabled) or 1 
 *                   (enabled).
 */
void libtwirc_on_roomstate(struct twirc_state *s, struct twirc_event *evt)
{
	evt->channel = evt->params[0];
}

/*
 * On any of the following events:
 * - Subscription, resubscription, or gift subscription to a channel.
 * - Incoming raid to a channel.
 * - Channel ritual. Many channels have special rituals to celebrate viewer 
 *   milestones when they are shared. The rituals notice extends the sharing of
 *   these messages to other viewer milestones (initially, a new viewer chatting
 *   for the first time).
 *
 * These fields are sent for all USERNOTICEs:
 * 
 * > @badges=<badges>;color=<color>;display-name=<display-name>;emotes=<emotes>;
 * id=<id-of-msg>;login=<user>;mod=<mod>;msg-id=<msg-id>;room-id=<room-id>;
 * subscriber=<subscriber>;system-msg=<system-msg>;tmi-sent-ts=<timestamp>;
 * turbo=<turbo>;user-id=<user-id>;user-type=<user-type> 
 *  :tmi.twitch.tv USERNOTICE #<channel> :<message>   
 *
 * Several other, msg-param fields are sent only for sub/resub, subgift, 
 * anonsubgift, raid, or ritual notices. See the table below for details. 
 *
 * badges:                           Comma-separated list of chat badges and the
 *                                   version of each badge (each in the format 
 *                                   <badge>/<version>, such as admin/1). Valid 
 *                                   badge values: admin, bits, broadcaster, 
 *                                   global_mod, moderator, subscriber, staff,
 *                                   turbo.
 * color:                            Hexadecimal RGB color code. This is empty if
 *                                   it is never set.
 * display-name:                     The user’s display name, escaped as described
 *                                   in the IRCv3 spec. This is empty if it is 
 *                                   never set.
 * emotes                            (see PRIVMSG)
 * id                                A unique ID for the message.
 * login                             The name of the user who sent the notice.
 * message                           The message. This is omitted if the user did
 *                                   not enter a message.
 * mod                               1 if the user has a moderator badge; 
 *                                   otherwise, 0.
 * msg-id                            The type of notice (not the ID). Valid values:
 *                                   sub, resub, subgift, anonsubgift, raid, ritual.
 * msg-param-cumulative-months:      (Sent only on sub, resub) The total number of
 *                                   months the user has subscribed. This is the 
 *                                   same as msg-param-months but sent for different
 *                                   types of user notices.
 * msg-param-displayName:            (Sent only on raid) The display name of the 
 *                                   source user raiding this channel.
 * msg-param-login:                  (Sent on only raid) The name of the source 
 *                                   user raiding this channel.
 * msg-param-months:                 (Sent only on subgift, anonsubgift) The total
 *                                   number of months the user has subscribed. This
 *                                   is the same as msg-param-cumulative-months but
 *                                   sent for different types of user notices.
 * msg-param-recipient-display-name: (Sent only on subgift, anonsubgift) The display
 *                                   name of the subscription gift recipient.
 * msg-param-recipient-id:           (Sent only on subgift, anonsubgift) The user ID
 *                                   of the subscription gift recipient.
 * msg-param-recipient-user-name:    (Sent only on subgift, anonsubgift) The user 
 *                                   name of the subscription gift recipient.
 * msg-param-should-share-streak:    (Sent only on sub, resub) Boolean indicating
 *                                   whether users want their streaks to be shared.
 * msg-param-streak-months:          (Sent only on sub, resub) The number of 
 *                                   consecutive months the user has subscribed. 
 *                                   This is 0 if msg-param-should-share-streak is 0.
 * msg-param-sub-plan:               (Sent only on sub, resub, subgift, anonsubgift)
 *                                   The type of subscription plan being used. 
 *                                   Valid values: Prime, 1000, 2000, 3000. 1000, 
 *                                   2000, and 3000 refer to the first, second, and 
 *                                   third levels of paid subscriptions, respectively
 *                                   (currently $4.99, $9.99, and $24.99).
 * msg-param-sub-plan-name           (Sent only on sub, resub, subgift, anonsubgift)
 *                                   The display name of the subscription plan. This 
 *                                   may be a default name or one created by the 
*                                    channel owner.
 * msg-param-viewerCount:            (Sent only on raid) The number of viewers 
 *                                   watching the source channel raiding this channel.
 * msg-param-ritual-name:            (Sent only on ritual) The name of the ritual 
 *                                   this notice is for. Valid value: new_chatter.
 * room-id:                          The channel ID.
 * system-msg:                       The message printed in chat along with this notice.
 * tmi-sent-ts:                      Timestamp when the server received the message.
 * user-id:                          The user’s ID.
 */
void libtwirc_on_usernotice(struct twirc_state *s, struct twirc_event *evt)
{
	evt->channel = evt->params[0];
	evt->message = evt->num_params > 1 ? evt->params[1] : NULL;
}

/*
 * When a user joins a channel or sends a PRIVMSG to a channel.
 *
 * > @badges=<badges>;color=<color>;display-name=<display-name>;
 *   emote-sets=<emotes>;mod=<mod>;subscriber=<subscriber>;turbo=<turbo>;
 *   user-type=<user-type> 
 *    :tmi.twitch.tv USERSTATE #<channel>
 *
 * badges:       Comma-separated list of chat badges and the version of each badge
 *               (each in the format <badge>/<version>, such as admin/1). 
 *               Valid badge values: admin, bits, broadcaster, global_mod, 
 *               moderator, subscriber, staff, turbo, vip.
 * color:        Hexadecimal RGB color code. This is empty if it is never set.
 * display-name: The user’s display name, escaped as described in the IRCv3 spec.
 *               This is empty if it is never set.
 * emotes:       Your emote set, a comma-separated list of emotes. This always 
 *               contains at least 0. Get Chat Emoticons by Set gets a subset of
 *               emoticon images.
 * mod:          1 if the user has a moderator badge; otherwise, 0.
 */
void libtwirc_on_userstate(struct twirc_state *s, struct twirc_event *evt)
{
	evt->channel = evt->params[0];
}

/*
 * This doesn't seem to be documented. Also, I've read in several places that
 * Twitch plans to move away from IRC for whispers (and whispers only). But for
 * now, sending a whisper to the bot does arrive as a regular IRC message, more
 * precisely as a WHISPER message. Example:
 *
 * https://discuss.dev.twitch.tv/t/what-are-specifics-of-irc-chat-and-whispering-noob-solved-i-think/6175/8
 *
 * @badges=;color=#DAA520;display-name=domsson;emotes=;message-id=7;
 *  thread-id=65269353_274538602;turbo=0;user-id=65269353;user-type= 
 *   :domsson!domsson@domsson.tmi.twitch.tv WHISPER kaulmate :hey kaul!
 */
void libtwirc_on_whisper(struct twirc_state *s, struct twirc_event *evt)
{
	evt->message = evt->params[evt->trailing];
}

/*
 * Handles all events that do not (yet) have a dedicated event handler.
 */
void libtwirc_on_other(struct twirc_state *s, struct twirc_event *evt)
{
	// TODO
}

void libtwirc_on_connect(struct twirc_state *s)
{
	// Set status to connected (discarding all other flags)
	s->status = TWIRC_STATUS_CONNECTED;

	// Start authentication process
	if (s->status & TWIRC_STATUS_AUTHENTICATING)
	{
		// Authentication has already been initialized!
		return;
	}
	if (s->status & TWIRC_STATUS_AUTHENTICATED)
	{
		// Already authenticated!
		return;
	}

	// Request capabilities before login, so that we will receive the
	// GLOBALUSERSTATE command on login in addition to the 001 (WELCOME)
	twirc_capreq(s);

	// Start authentication process (user login)
	twirc_auth(s);
}

void libtwirc_on_disconnect(struct twirc_state *s)
{
	// Set status to disconnected (discarding all other flags)
	s->status = TWIRC_STATUS_DISCONNECTED;
	// Set running to 0 so that our main loop stops
	s->running = 0;
}

/*
 * Authenticates with the Twitch Server using the NICK and PASS commands.
 * You are not automatically authenticated when this function returns,
 * you need to wait for the server's reply first. If the tags capability 
 * has been requested and acknoweldged before, the server will confirm 
 * login (authentication) with the GLOBALUSERSTATE command, otherwise we
 * can just look out for the MOTD (starting with numeric command 001).
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
	
	s->status |= TWIRC_STATUS_AUTHENTICATING;
	return 0;
}

/*
 * Sends the QUIT command to the server, then terminates the connection.
 * Returns 0 on success, -1 on error (see errno).
 */ 
int twirc_disconnect(struct twirc_state *state)
{
	// Say bye-bye to the IRC server
	twirc_cmd_quit(state);
	
	// Close the socket
	int err = tcpsnob_close(state->socket_fd);
	
	// Run the disconnect event handlers
	libtwirc_on_disconnect(s);
	s->cbs.disconnect(s, NULL);
	
	// Account for close() running into an error
	if (err == -1)
	{
		s->error = TWIRC_ERR_SOCKET_CLOSE;
	}
	return err;
}

/*
 * Sets all callback members to the dummy callback
 */
void twirc_init_callbacks(struct twirc_callbacks *cbs)
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
}

/*
 * Returns a pointer to the state's twirc_callbacks structure. 
 * This allows the user to set select callbacks to their handler functions.
 * Under no circumstances should the user set any callback to NULL, as this
 * will eventually lead to a segmentation fault, as libtwirc relies on the
 * fact that every callback that wasn't assigned to by the user is assigned
 * to an internal dummy (null) event handler.
 */
struct twirc_callbacks *twirc_get_callbacks(struct twirc_state *s)
{
	return &s->cbs;
}

/*
 * Returns a pointer to a twirc_state struct, which represents the state of
 * the connection to the server, the state of the user, holds the login data,
 * all callback function pointers for event handling and much more. Returns
 * a NULL pointer if any errors occur during initialization.
 */
struct twirc_state* twirc_init()
{
	// Init state struct
	struct twirc_state *state = malloc(sizeof(struct twirc_state));
	if (state == NULL) { return NULL; }
	memset(state, 0, sizeof(struct twirc_state));

	// Set some defaults / initial values
	state->status    = TWIRC_STATUS_DISCONNECTED;
	state->ip_type   = TWIRC_IPV4;
	state->socket_fd = -1;
	state->error     = 0;
	
	// Initialize the buffer - it will be twice the message size so it can
	// easily hold an incomplete message in addition to a complete one
	state->buffer = malloc(2 * TWIRC_MESSAGE_SIZE * sizeof(char));
	if (state->buffer == NULL) { return NULL; }
	state->buffer[0] = '\0';

	// Make sure the structs within state are zero-initialized
	memset(&state->login, 0, sizeof(struct twirc_login));
	memset(&state->user, 0, sizeof(struct twirc_user));
	memset(&state->cbs, 0, sizeof(struct twirc_callbacks));

	// Set all callbacks to the dummy callback
	twirc_init_callbacks(&state->cbs);

	// All done
	return state;
}

void libtwirc_free_callbacks(struct twirc_state *state)
{
	// We do not need to free the callback pointers, as they are function 
	// pointers and were therefore not allocated with malloc() or similar.
	// However, let's make sure we 'forget' the currently assigned funcs.
	memset(&state->cbs, 0, sizeof(struct twirc_callbacks));
}

void libtwirc_free_login(struct twirc_state *state)
{
	free(state->login.host);
	free(state->login.port);
	free(state->login.nick);
	free(state->login.pass);
	state->login.host = NULL;
	state->login.port = NULL;
	state->login.nick = NULL;
	state->login.pass = NULL;
}

void libtwirc_free_user(struct twirc_state *state)
{
	free(state->user.name);
	free(state->user.id);
	state->user.name = NULL;
	state->user.id   = NULL;
}

/*
 * Frees the twirc_state and all of its members.
 */
int twirc_free(struct twirc_state *state)
{
	libtwirc_free_callbacks(state);
	libtwirc_free_login(state);
	libtwirc_free_user(state);
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

/*
 * TODO: AFAIK, this is untested as of yet! Damn.
 */
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

/*
 * Extracts the nickname from an IRC message's prefix, if any. Done this way:
 * Searches prefix for an exclamation mark ('!'). If there is one, everything 
 * before it will be returned as a pointer to an allocated string (malloc), so
 * the caller has to free() it at some point. If there is no exclamation mark 
 * in prefix or prefix is NULL or we're out of memory, NULL will be returned.
 */
char *libtwirc_parse_nick(const char *prefix)
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
 * value of a tag, respectively. The value member of a tag can be NULL for tags 
 * that are key-only. The last element of the array will be a NULL pointer, so 
 * you can loop over all tags until you hit NULL. The number of extracted tags
 * is returned in len. If no tags have been found at the beginning of msg, tags
 * will be NULL, len will be 0 and this function will return a pointer to msg.
 * Otherwise, a pointer to the part of msg after the tags will be returned. 
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

/*
 * Checks if the event is a CTCP event. If so, strips the CTCP markers (0x01)
 * as well as the CTCP command from the trailing parameter and fills the ctcp
 * member of evt with the CTCP command instead. If it isn't a CTCP command, 
 * this function does nothing.
 * Returns 0 on success, -1 if an error occured (not enough memory).
 */
int libtwirc_parse_ctcp(struct twirc_event *evt)
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

/*
 * Dispatches the internal and external event handler / callback functions
 * for the given event, based on the command field of evt. Does not handle
 * CTCP events - call libtwirc_dispatch_ctcp() for those instead.
 */
void libtwirc_dispatch_evt(struct twirc_state *state, struct twirc_event *evt)
{
	// TODO order these by most frequent (statistically), so that we waste as
	// little CPU cycles as possible on string comparison via strcmp() here!
	
	if (strcmp(evt->command, "PRIVMSG") == 0)
	{
		libtwirc_on_privmsg(state, evt);
		state->cbs.privmsg(state, evt);
		return;
	}
	if (strcmp(evt->command, "JOIN") == 0)
	{
		libtwirc_on_join(state, evt);
		state->cbs.join(state, evt);
		return;
	}
	if (strcmp(evt->command, "PART") == 0)
	{
		libtwirc_on_part(state, evt);
		state->cbs.join(state, evt);
		return;
	}
	if (strcmp(evt->command, "PING") == 0)
	{
		libtwirc_on_ping(state, evt);
		state->cbs.ping(state, evt);
		return;
	}
	if (strcmp(evt->command, "MODE") == 0)
	{
		libtwirc_on_mode(state, evt);
		state->cbs.mode(state, evt);
		return;
	}
	if (strcmp(evt->command, "353") == 0 ||
	    strcmp(evt->command, "366") == 0))
	{
		libtwirc_on_names(state, evt);
		state->cbs.names(state, evt);
		return;
	}
	if (strcmp(evt->command, "WHISPER") == 0)
	{
		libtwirc_on_whisper(state, evt);
		state->cbs.whisper(state, evt);
		return;
	}
	if (strcmp(evt->command, "NOTICE") == 0)
	{	
		libtwirc_on_notice(state, evt);
		state->cbs.notice(state, evt);
		return;
	}
	if (strcmp(evt->command, "ROOMSTATE") == 0)
	{		
		libtwirc_on_roomstate(state, evt);
		state->cbs.roomstate(state, evt);
		return;
	}
	if (strcmp(evt->command, "USERSTATE") == 0)
	{		
		libtwirc_on_userstate(state, evt);
		state->cbs.userstate(state, evt);
		return;
	}
	if (strcmp(evt->command, "USERNOTICE") == 0)
	{		
		libtwirc_on_usernotice(state, evt);
		state->cbs.usernotice(state, evt);
		return;
	}
	if (strcmp(evt->command, "HOSTTARGET") == 0)
	{
		libtwirc_on_hosttarget(state, evt);
		state->cbs.hosttarget(state, evt);		
		return;
	}
	if (strcmp(evt->command, "CLEARCHAT") == 0)
	{
		libtwirc_on_clearchat(state, evt);
		state->cbs.clearchat(state, evt);
		return;
	}
	if (strcmp(evt->command, "CLEARMSG") == 0)
	{		
		libtwirc_on_clearmsg(state, evt);
		state->cbs.clearmsg(state, evt);
		return;
	}
	if (strcmp(evt->command, "CAP") == 0 &&
	    strcmp(evt->params[0], "*") == 0)
	{
		libtwirc_on_capack(state, evt);
		state->cbs.capack(state, evt);
		return;
	}
	if (strcmp(evt->command, "001") == 0)
	{
		libtwirc_on_welcome(state, evt);
		state->cbs.welcome(state, evt);
		return;
	}
	if (strcmp(evt->command, "GLOBALUSERSTATE") == 0)
	{
		libtwirc_on_globaluserstate(state, evt);
		state->cbs.globaluserstate(state, evt);
		return;
	}
	if (strcmp(evt->command, "421") == 0)
	{
		libtwirc_on_invalidcmd(state, evt);
		state->cbs.invalidcmd(state, evt);
		return;
	}
	if (strcmp(evt->command, "RECONNECT") == 0)
	{
		libtwirc_on_reconnect(state, evt);
		state->cbs.reconnect(state, evt);
		return;
	}
	
	// Some unaccounted-for event occured
	libtwirc_on_other(state, evt);
	state->cbs.other(state, evt);
}

/*
 * Dispatches the internal and external event handler / callback functions
 * for the given CTCP event, based on the ctcp field of evt. Does not handle
 * regular events - call libtwirc_dispatch_evt() for those instead.
 */
void libtwirc_dispatch_ctcp(struct twirc_state *state, struct twirc_event *evt)
{
	if (strcmp(evt->ctcp, "ACTION") == 0)
	{
		libtwirc_on_action(state, evt);
		state->cbs.action(state, evt);
		return;
	}
	
	// Some unaccounted-for event occured
	libtwirc_on_other(state, evt);
	state->cbs.other(state, evt);
}

/*
 * Takes a raw IRC message and parses all the relevant information into a 
 * twirc_event struct, then calls upon the functions responsible for the 
 * dispatching of the event to internal and external callback functions.
 * Returns 0 on success, -1 if an out of memory error occured during the
 * parsing/handling of a CTCP event.
 */
int libtwirc_process_msg(struct twirc_state *s, const char *msg)
{
	//fprintf(stderr, "> %s (%zu)\n", msg, strlen(msg));

	int err = 0;
	struct twirc_event evt = { 0 };

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
	evt.nick = libtwirc_parse_nick(evt.prefix);
	
	if (evt.ctcp)
	{
		libtwirc_dispatch_ctcp(s, &evt);
	}
	else
	{
		libtwirc_dispatch_evt(s, &evt);
	}

	// Free event
	// TODO: make all of this into a function? libtwirc_free_event()
	libtwirc_free_tags(evt.tags);
	free(evt.prefix);
	free(evt.nick);
	free(evt.command);
	free(evt.ctcp);
	libtwirc_free_params(evt.params);

	return err;
}

/*
 * Process the received data in buf, which has a size of len bytes.
 * Incomplete commands will be buffered in state->buffer, complete commands
 * will be processed right away.
 * TODO comments: return values etc
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
	if (chunk == NULL) { return -1; }
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
		// Process the message and check if we ran out of memory doing so
		if (libtwirc_process_msg(state, msg) == -1)
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
int libtwirc_handle_event(struct twirc_state *s, struct epoll_event *epev)
{
	// We've got data coming in
	if(epev->events & EPOLLIN)
	{
		char buf[TWIRC_BUFFER_SIZE];
		int bytes_received = 0;
		
		// Fetch and process all available data from the socket
		while ((bytes_received = twirc_recv(s, buf, TWIRC_BUFFER_SIZE)) > 0)
		{
			// Process the data and check if we ran out of memory doing so
			if (libtwirc_process_data(s, buf, bytes_received) == -1)
			{
				s->error = TWIRC_ERR_OUT_OF_MEMORY;
				return -1;
			}
		}
		
		// If twirc_recv() returned -1, the connection is probably down!
		if (bytes_received == -1 &&
            (errno == EBADF ||         // Invalid socket
			 errno == ECONNREFUSED ||  // Connection refused
			 errno == ENOTCONN))       // Socket not connected
		{
			// TODO don't we need to call disconnect event handlers here?
			s->error = TWIRC_ERR_SOCKET_RECV;
			return -1;
		}
	}
	
	// We're ready to send data
	if (epev->events & EPOLLOUT)
	{
		// If we weren't connected yet, we seem to be now!
		if (s->status & TWIRC_STATUS_CONNECTING)
		{		
			// Call internal connect event handler
			// It sets the status to connected and is supposed to
			// request capabilities and initiate the authentication
			libtwirc_on_connect(s);

			// Call user's connect event handler
			// Although I don't see what the user would want to do 
			// on connect; the welcome event, which is dispatched 
			// once authentication has happened, is way more useful
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
	
	// Handled everything and no disconnect detected
	return 0;
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
		// TODO shouldn't we check if we're already connected and, if so,
		//      call the disconnect event handlers here? Or maybe not, as
		//      an error in epoll_wait doesn't necessarily mean that we 
		//      actually lost our connection - but how should we handle
		//      this (hopefully rather exotic) situation properly?
		s->error = TWIRC_ERR_EPOLL_WAIT;
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
 * set to 0, the loop is ended and this function returns with a value of 0.
 * TODO: the code doesn't actually respect the above comment: we run as long
 *       as 'running' is set to 1, we don't even check the return value of 
 *       twirc_tick() - however, isn't it too much to check for both anyway?
 *       Can't we just do: `while(twirc_tick() == 0)` instead and drop the 
 *       running flag altogether? Or the other way round, make sure that if 
 *       the connection is dropped OR an error occurs, the running flag is 
 *       always set to 0 for sure (maybe that's already the case, probably)
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

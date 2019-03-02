#include <stdio.h>      // NULL, fprintf(), perror()
/*
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
*/
#include "libtwirc.h"

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
 * Sends the PING command to the IRC server. 
 * If param is given, it will be appended.
 * Returns 0 on success, -1 otherwise.
 */
int twirc_cmd_ping(struct twirc_state *state, const char *param)
{
	// "PING <param>"
	char ping[TWIRC_PONG_SIZE];
	ping[0] = '\0';
	snprintf(ping, TWIRC_PONG_SIZE, "PING %s", param ? param : "");
	return twirc_send(state, ping);
}

/*
 * Sends the QUIT command to the IRC server.
 * Returns 0 on success, -1 otherwise. 
 */
int twirc_cmd_quit(struct twirc_state *state)
{
	return twirc_send(state, "QUIT");
}


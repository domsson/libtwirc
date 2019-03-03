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
 * Sends the PONG command to the IRC server.
 * If param is given, it will be appended. To make Twitch happy (this is not 
 * part of the IRC specification) the param will be prefixed with a colon (":")
 * unless it is prefixed with one already.
 * Returns 0 on success, -1 otherwise.
 */
int twirc_cmd_pong(struct twirc_state *state, const char *param)
{
	// "PONG :<param>"
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
	// Usage: "/w <login> <message>"
	char whisper[TWIRC_MESSAGE_SIZE];
	snprintf(whisper, TWIRC_MESSAGE_SIZE, "PRIVMSG %s :/w %s %s", 
			TWIRC_CMD_CHANNEL, nick, msg);
	return twirc_send(state, whisper);
}

/*
 * Requests a list of the channel's moderators, both offline and online.
 * The answer will be in the form of a NOTICE with the msg-id tag set to 
 * "room_mods" and a message like "The moderators of this channel are: <list>",
 * where list is a comma-and-space separated list of the moderators nicks.
 */
int twirc_cmd_mods(struct twirc_state *state, const char *chan)
{
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/mods", chan);
	return twirc_send(state, privmsg);
}

/*
 * Change your color to the specified one. If you're a turbo user, this can be
 * any hex color (for example, "#FFFFFF" for white), otherwise it should be a 
 * named color from the following list (might change in the future):
 *   Blue, BlueViolet, CadetBlue, Chocolate, Coral, DodgerBlue, Firebrick, 
 *   GoldenRod, Green, HotPink, OrangeRed, Red, SeaGreen, SpringGreen,
 *   YellowGreen
 */
int twirc_cmd_color(struct twirc_state *state, const char *color)
{
	// Usage: "/color <color>" - Change your username color. Color must be 
	// in hex (#000000) or one of the following: Blue, BlueViolet, CadetBlue,
	// Chocolate, Coral, DodgerBlue, Firebrick, GoldenRod, Green, HotPink, 
	// OrangeRed, Red, SeaGreen, SpringGreen, YellowGreen.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/color %s", 
			TWIRC_CMD_CHANNEL, color);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Timeout the user with the given nick name in the specified channel for 
 * `secs` amount of seconds. If `secs` is 0, the Twitch default will be used, 
 * which is 600 seconds (10 minutes) at the time of writing. `reason` will be 
 * shown to the affected user and other moderators and is optional (use NULL).
 */
int twirc_cmd_timeout(struct twirc_state *state, const char *chan, const char *nick, int secs, const char *reason)
{
	// Usage: "/timeout <username> [duration][time unit] [reason]"
	// Temporarily prevent a user from chatting. Duration (optional, 
	// default=10 minutes) must be a positive integer; time unit (optional, 
	// default=s) must be one of s, m, h, d, w; maximum duration is 2 weeks.
	// Combinations like 1d2h are also allowed. Reason is optional and will 
	// be shown to the target user and other moderators. Use "untimeout" to 
	// remove a timeout.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/timeout %s %.0d %s", 
			chan, nick, secs, reason == NULL ? "" : reason);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Un-timeout the given user in the given channel. You could use unban as well.
 */
int twirc_cmd_untimeout(struct twirc_state *state, const char *chan, const char *nick)
{
	// Usage: "/untimeout <username>" - Removes a timeout on a user.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/untimeout %s",
			chan, nick);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Permanently ban the specified user from the specified channel. Optionally, a
 * reason can be given, which will be shown to the affected user and other 
 * moderators. If you don't want to give a reason, set it to NULL.
 */
int twirc_cmd_ban(struct twirc_state *state, const char *chan, const char *nick, const char *reason)
{
	// Usage: "/ban <username> [reason]" - Permanently prevent a user from
	// chatting. Reason is optional and will be shown to the target user 
	// and other moderators. Use "unban" to remove a ban.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/ban %s %s", 
			chan, nick, reason == NULL ? "" : reason);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Unban the specified user from the specified channel. Also removes timeouts.
 */
int twirc_cmd_unban(struct twirc_state *state, const char *chan, const char *nick)
{
	// Usage: "/unban <username>" - Removes a ban on a user.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/unban %s",
			chan, nick);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Enable slow mode in the specified channel. This means users can only send 
 * messages every `secs` seconds. If seconds is 0, the Twitch default, which at 
 * the time of writing is 120 seconds, will be used.
 */
int twirc_cmd_slow(struct twirc_state *state, const char *chan, int secs)
{
	// Usage: "/slow [duration]" - Enables slow mode (limit how often users
	// may send messages). Duration (optional, default=120) must be a 
	// positive number of seconds. Use "slowoff" to disable.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/slow %.0d",
			chan, secs);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Disables slow mode in the specified channel.
 */
int twirc_cmd_slowoff(struct twirc_state *state, const char *chan)
{
	// Usage: "/slowoff" - Disables slow mode.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/slowoff", chan);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Enables followers-only mode for the specified channel.
 * This means that only followers can still send messages. If `time` is set, 
 * only followers who have been following for at least the specified time will 
 * be allowed to chat. Allowed values range from 0 minutes (all followers) to 
 * 3 months. `time` should be NULL or a string as in the following examples: 
 * "7m" or "7 minutes", "2h" or "2 hours", "5d" or "5 days", "1w" or "1 week", 
 * "3mo" or "3 months".
 */
int twirc_cmd_followers(struct twirc_state *state, const char *chan, const char *time)
{
	// Usage: "/followers [duration]" - Enables followers-only mode (only 
	// users who have followed for 'duration' may chat). Examples: "30m", 
	// "1 week", "5 days 12 hours". Must be less than 3 months.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/followers %s", 
			chan, time == NULL ? "" : time);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Disable followers-only mode for the specified channel.
 */
int twirc_cmd_followersoff(struct twirc_state *state, const char *chan)
{
	// Usage: "/followersoff - Disables followers-only mode.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/followersoff", chan);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Enable subscriber-only mode for the specified channel.
 */
int twirc_cmd_subscribers(struct twirc_state *state, const char *chan)
{
	// Usage: "/subscribers" - Enables subscribers-only mode (only subs 
	// may chat in this channel). Use "subscribersoff" to disable.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/subscribers", chan);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Disable subscriber-only mode for the specified channel.
 */
int twirc_cmd_subscribersoff(struct twirc_state *state, const char *chan)
{
	// Usage: "/subscribersoff" - Disables subscribers-only mode.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/subscribersoff", chan);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Completely wipe the previous chat history. Note: clients can ignore this.
 */
int twirc_cmd_clear(struct twirc_state *state, const char *chan)
{
	// Usage: "/clear" - Clear chat history for all users in this room.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/clear", chan);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Enables R9K mode for the specified channel.
 * Check the Twitch docs for further information about R9K mode.
 */
int twirc_cmd_r9k(struct twirc_state *state, const char *chan)
{
	// Usage: "/r9kbeta" - Enables r9k mode. Use "r9kbetaoff" to disable.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/r9kbeta", chan);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Disables R9K mode for the specified channel.
 */
int twirc_cmd_r9koff(struct twirc_state *state, const char *chan)
{
	// Usage: "/r9kbetaoff" - Disables r9k mode.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/r9kbetaoff", chan);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Enables emote-only mode for the specified channel.
 * This means that only messages that are 100% emotes are allowed.
 */
int twirc_cmd_emoteonly(struct twirc_state *state, const char *chan)
{
	// Usage: "/emoteonly" - Enables emote-only mode (only emoticons may 
	// be used in chat). Use "emoteonlyoff" to disable.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/emoteonly", chan);
	return twirc_send(state, privmsg);
}

/*
 * Broadcasters and Moderators only:
 * Disables emote-only mode for the specified channel.
 */
int twirc_cmd_emoteonlyoff(struct twirc_state *state, const char *chan)
{
	// Usage: "/emoteonlyoff" - Disables emote-only mode.
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/emoteonlyoff", chan);
	return twirc_send(state, privmsg);
}

/*
 * Partner only:
 * Run a commercial for all viewers for `secs` seconds. `secs` can be 0, in 
 * which case the Twitch default (30 secs at time of writing) will be used;
 * otherwise the following values are allowed: 30, 60, 90, 120, 150, 180.
 */
int twird_cmd_commercial(struct twirc_state *state, const char *chan, int secs)
{
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/commercial %.0d",
			chan, secs);
	return twirc_send(state, privmsg);
}

/*
 * Broadcaster and channel editor only:
 * Host the channel of the user given via `target`.
 * Note: target channel has to be given without the pound sign ("#").
 */
int twirc_cmd_host(struct twirc_state *state, const char *chan, const char *target)
{
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/host %s",
			chan, target);
	return twirc_send(state, privmsg);
}

/*
 * Broadcaster and channel editor only:
 * Stop hosting a channel and return to the normal state.
 */
int twirc_cmd_unhost(struct twirc_state *state, const char *chan)
{
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/unhost", chan);
	return twirc_send(state, privmsg);
}

/*
 * Broadcaster only:
 * Promote the user with the given nick to channel moderator.
 */
int twirc_cmd_mod(struct twirc_state *state, const char *chan, const char *nick)
{
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/mod %s", chan, nick);
	return twirc_send(state, privmsg);
}

/*
 * Broadcaster only:
 * Demote the moderator with the given nick back to a regular viewer.
 */
int twirc_cmd_unmod(struct twirc_state *state, const char *chan, const char *nick)
{
	char privmsg[TWIRC_BUFFER_SIZE];
	snprintf(privmsg, TWIRC_BUFFER_SIZE, "PRIVMSG %s :/unmod %s", chan, nick);
	return twirc_send(state, privmsg);
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



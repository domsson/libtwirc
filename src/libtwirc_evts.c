#include <stdlib.h>     // NULL, EXIT_FAILURE, EXIT_SUCCESS
#include <string.h>     // strlen(), strerror()
#include "libtwirc.h"

/*
 * This dummy callback function does absolutely nothing.
 * However, it allows us to make sure that all callbacks are non-NULL, removing 
 * the need to check for NULL everytime before we call them. It makes the code 
 * cleaner but brings the overhead of a function call instead of a NULL-check.
 * I prefer this approach as long we don't run into any performance problems. 
 * Maybe `static inline` enables the compiler to optimize the overhead.
 */
static inline
void libtwirc_on_null(struct twirc_state *s, struct twirc_event *evt)
{
	// Nothing in here - that's on purpose
}

/*
 * Is being called for every message we sent to the IRC server.
 */
void libtwirc_on_outgoing(struct twirc_state *s, struct twirc_event *evt)
{
	// There should be nothing to do here
}

/*
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
	// TODO fill the twirc_user_t with information from this event
}

/*
 * On "CAP * ACK" command, which confirms a requested capability.
 */
void libtwirc_on_capack(struct twirc_state *s, struct twirc_event *evt)
{
	// Maybe we should keep track of what capabilities have been 
	// acknowledged by the server, so the user can query it if need be?
}

/*
 * Responds to incoming PING commands with a corresponding PONG.
 */
void libtwirc_on_ping(struct twirc_state *s, struct twirc_event *evt)
{
	twirc_cmd_pong(s, evt->num_params ? evt->params[0] : NULL);
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
	if (evt->num_params < 2)
	{
		return;
	}
	if (evt->params[1][0] == '=' && evt->num_params >= 3)
	{
		evt->channel = evt->params[2];
	}
	else
	{
		evt->channel = evt->params[1];
	}
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
 * According to actual tests, a /ban command will emit the following two messages:
 * > @room-id=<room-id>;target-user-id=<user-id>;tmi-sent-ts=<timestamp> 
 *   :tmi.twitch.tv CLEARCHAT #<channel> :<user>
 * > @msg-id=ban_success :tmi.twitch.tv NOTICE #<channel> 
 *   :<user> is now banned from this channel.
 *
 * Also, I've figured out that the CLEARCHAT message will also be triggered
 * when a mod issued the /clear command to clear the entire chat history:
 * > @room-id=<room-id>;tmi-sent-ts=<timestamp> :tmi.twitch.tv CLEARCHAT #<channel>
 * 
 * ban-duration: (Optional) Duration of the timeout, in seconds.
 *               If omitted, the ban is permanent.
 *
 * Note that there is no way to figure out who banned the user. This is by
 * design as "users could scrape it, and use it to target the mods that timed 
 * them out or banned them." 
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
 * Example (as seen):
 * > :tmi.twitch.tv HOSTTARGET #domsson :fujioka_twitch -
 *
 * Example (as seen) for host start:
 * > :tmi.twitch.tv HOSTTARGET #domsson :bawnsai 0
 * > @msg-id=host_on :tmi.twitch.tv NOTICE #domsson :Now hosting bawnsai.
 *
 * Example (as seen) for host stop:
 * > :tmi.twitch.tv HOSTTARGET #domsson :- 0
 * > @msg-id=host_off :tmi.twitch.tv NOTICE #domsson :Exited host mode.
 *
 */
void libtwirc_on_hosttarget(struct twirc_state *s, struct twirc_event *evt)
{
	// TODO got to figure out the exact syntax:
	//      - Channels without # ?
	//      - number-of-viewers is '-' when not given?
	//      - channel and number-of-viewers are together the trailing param?
	evt->channel = evt->params[0];

	char *sp = strstr(evt->params[1], " ");
	if (sp == NULL)
	{
		return;
	}
	evt->target = strndup(evt->params[1], sp - evt->params[1] + 1);
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
	
	// Close the socket (this might fail as it might be closed already);
	// we're not checking for that error and therefore we don't report 
	// the error via s->error for two reasons: first, we kind of expect 
	// this to fail; second: we don't want to override more meaningful 
	// errors that might have occurred before 
	tcpsnob_close(s->socket_fd);
}


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
static inline void 
libtwirc_on_null(twirc_state_t *s, twirc_event_t *evt)
{
	// Nothing in here - that's on purpose
}

/*
 * Is being called for every message we sent to the IRC server. Note that the 
 * convenience members of the event struct ("nick", "channel", etc) will all
 * be NULL, as we're not looking at what kind of command/message was sent. 
 * The raw message, as well as the raw parts ("prefix", "command", etc) will
 * all be available, however.
 */
static void 
libtwirc_on_outbound(twirc_state_t *s, twirc_event_t *evt)
{
	// Nothing, otherwise we'd have to have a ton of if and else
}

/*
 * If you send an invalid command, you will get a 421 message back:
 *
 * < WHO #<channel>
 * > :tmi.twitch.tv 421 <user> WHO :Unknown command
 */
static void
libtwirc_on_invalidcmd(twirc_state_t *s, twirc_event_t *evt)
{
	// Don't think we have to do anything here, honestly
}

/*
 * Handler for the "001" command (RPL_WELCOME), which the Twitch servers send
 * on successful login, even when no capabilities have been requested.
 */
static void
libtwirc_on_welcome(twirc_state_t *s, twirc_event_t *evt)
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
static void
libtwirc_on_globaluserstate(twirc_state_t *s, twirc_event_t *evt)
{
	s->status |= TWIRC_STATUS_AUTHENTICATED;
	
	// Save the display-name and user-id in our login struct
	twirc_tag_t *name = twirc_get_tag_by_key(evt->tags, "display-name");
	twirc_tag_t *id   = twirc_get_tag_by_key(evt->tags, "user-id");
	s->login.name = name ? strdup(name->value) : NULL;
	s->login.id   = id   ? strdup(id->value)   : NULL;
}

/*
 * On "CAP * ACK" command, which confirms a requested capability.
 */
static void
libtwirc_on_capack(twirc_state_t *s, twirc_event_t *evt)
{
	// TODO Maybe we should keep track of what capabilities have been 
	// acknowledged by the server, so the user can query it if need be?
}

/*
 * Responds to incoming PING commands with a corresponding PONG.
 */
static void
libtwirc_on_ping(twirc_state_t *s, twirc_event_t *evt)
{
	twirc_cmd_pong(s, evt->num_params ? evt->params[0] : NULL);
}

/*
 * When a user joins a channel we are in. The user might be us.
 * 
 * > :<user>!<user>@<user>.tmi.twitch.tv JOIN #<channel>
 */
static void
libtwirc_on_join(twirc_state_t *s, twirc_event_t *evt)
{
	if (evt->num_params > 0)
	{
		evt->channel = evt->params[0];
	}
}

/*
 * Gain/lose moderator (operator) status in a channel.
 *
 * > :jtv MODE #<channel> +o <user>
 * > :jtv MODE #<channel> -o <user>
 * 
 * Note: I've never actually seen this message being sent, even when
 *       giving/revoking mod status to/from the test bot.
 * 
 * Update: I've now seen this happen after joining a channel... ONCE.
 *         It happened on the #esl_csgo channel, where after join, I first 
 *         received the NAMES list, followed by this, which happened to be 
 *         the list of all moderators in the channel at that point in time:
 *
 * > :jtv MODE #esl_csgo +o logviewer
 * > :jtv MODE #esl_csgo +o feralhelena
 * > :jtv MODE #esl_csgo +o moobot
 * > :jtv MODE #esl_csgo +o general23497
 * > :jtv MODE #esl_csgo +o xhipgamer
 * > :jtv MODE #esl_csgo +o xzii
 * > :jtv MODE #esl_csgo +o 2divine
 * > :jtv MODE #esl_csgo +o cent
 * > :jtv MODE #esl_csgo +o x_samix_x
 * > :jtv MODE #esl_csgo +o ravager01
 * > :jtv MODE #esl_csgo +o doctorwigglez
 */
static void 
libtwirc_on_mode(twirc_state_t *s, twirc_event_t *evt)
{
	if (evt->num_params > 0)
	{
		evt->channel = evt->params[0];
	}
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
static void 
libtwirc_on_names(twirc_state_t *s, twirc_event_t *evt)
{
	if (strcmp(evt->command, "353") == 0 && evt->num_params > 2)
	{
		evt->channel = evt->params[2];
		return;
	}
	if (strcmp(evt->command, "366") == 0 && evt->num_params > 1)
	{
		evt->channel = evt->params[1];
		return;
	}
}

/*
 * Depart from a channel.
 *
 * > :<user>!<user>@<user>.tmi.twitch.tv PART #<channel>
 */
static void 
libtwirc_on_part(twirc_state_t *s, twirc_event_t *evt)
{
	if (evt->num_params > 0)
	{
		evt->channel = evt->params[0];
	}
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
static void 
libtwirc_on_clearchat(twirc_state_t *s, twirc_event_t *evt)
{
	if (evt->num_params > 0)
	{
		evt->channel = evt->params[0];
	}
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
static void 
libtwirc_on_clearmsg(twirc_state_t *s, twirc_event_t *evt)
{
	if (evt->num_params > 0)
	{
		evt->channel = evt->params[0];
	}
	if (evt->num_params > evt->trailing)
	{
		evt->message = evt->params[evt->trailing];
	}
}

/*
 * A joined channel starts or stops host mode.
 *
 * Start:
 * > :tmi.twitch.tv HOSTTARGET #hosting_channel <channel> [<number-of-viewers>]
 *
 * Stop:
 * > :tmi.twitch.tv HOSTTARGET #hosting_channel :- [<number-of-viewers>]
 *
 * number-of-viewers: (Optional) Number of viewers watching the host.
 *
 * Example (as seen) for host start:
 * > :tmi.twitch.tv HOSTTARGET #domsson :foxxwounds -
 * @msg-id=host_on :tmi.twitch.tv NOTICE #domsson :Now hosting foxxwounds.
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
static void
libtwirc_on_hosttarget(twirc_state_t *s, twirc_event_t *evt)
{
	if (evt->num_params > 0)
	{
		evt->channel = evt->params[0];
	}

	// If there is no trailing parameter, we exit early
	if (evt->num_params <= evt->trailing)
	{
		return;
	}

	// Check if there is a space in the trailing parameter
	char *sp = strstr(evt->params[evt->trailing], " ");
	if (sp == NULL) { return; }
	
	// Extract the username from the trailing parameter
	evt->target = strndup(evt->params[evt->trailing], 
			sp - evt->params[evt->trailing] + 1);
	
	// If the username was "-", we set it to NULL for better indication
	if (strcmp(evt->target, "-") == 0)
	{
		free(evt->target);
		evt->target = NULL;
	}
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
static void
libtwirc_on_notice(twirc_state_t *s, twirc_event_t *evt)
{
	if (evt->num_params > 0)
	{
		evt->channel = evt->params[0];
	}
	if (evt->num_params > evt->trailing)
	{
		evt->message = evt->params[evt->trailing];
	}
}

/*
 * CTCP ACTION
 */
static void 
libtwirc_on_action(twirc_state_t *s, twirc_event_t *evt)
{
	if (evt->num_params > 0)
	{
		evt->channel = evt->params[0];
	}
	if (evt->num_params > evt->trailing)
	{
		evt->message = evt->params[evt->trailing];
	}
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
static void
libtwirc_on_reconnect(twirc_state_t *s, twirc_event_t *evt)
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
 * tmi-sent-ts:  Timestamp when the server received the message.
 * user-id:      The user’s ID.
 */
static void
libtwirc_on_privmsg(twirc_state_t *s, twirc_event_t *evt)
{
	if (evt->num_params > 0)
	{
		evt->channel = evt->params[0];
	}
	if (evt->num_params > evt->trailing)
	{
		evt->message = evt->params[evt->trailing];
	}
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
static void
libtwirc_on_roomstate(twirc_state_t *s, twirc_event_t *evt)
{
	if (evt->num_params > 0)
	{
		evt->channel = evt->params[0];
	}
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
 *                                   channel owner.
 * msg-param-viewerCount:            (Sent only on raid) The number of viewers 
 *                                   watching the source channel raiding this channel.
 * msg-param-ritual-name:            (Sent only on ritual) The name of the ritual 
 *                                   this notice is for. Valid value: new_chatter.
 * room-id:                          The channel ID.
 * system-msg:                       The message printed in chat along with this notice.
 * tmi-sent-ts:                      Timestamp when the server received the message.
 * user-id:                          The user’s ID.
 */
static void
libtwirc_on_usernotice(twirc_state_t *s, twirc_event_t *evt)
{
	if (evt->num_params > 0)
	{
		evt->channel = evt->params[0];
	}
	if (evt->num_params > evt->trailing)
	{
		evt->message = evt->params[evt->trailing];
	}
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
static void
libtwirc_on_userstate(twirc_state_t *s, twirc_event_t *evt)
{
	if (evt->num_params > 0)
	{
		evt->channel = evt->params[0];
	}
}

/*
 * This doesn't seem to be documented. Also, I've read in several places that
 * Twitch plans to move away from IRC for whispers (and whispers only). But for
 * now, sending a whisper to the bot does arrive as a regular IRC message, more
 * precisely as a WHISPER message. Example:
 *
 * @badges=;color=#DAA520;display-name=domsson;emotes=;message-id=7;
 *  thread-id=65269353_274538602;turbo=0;user-id=65269353;user-type= 
 *   :domsson!domsson@domsson.tmi.twitch.tv WHISPER kaulmate :hey kaul!
 */
static void
libtwirc_on_whisper(twirc_state_t *s, twirc_event_t *evt)
{
	if (evt->num_params > 0)
	{
		evt->target  = strdup(evt->params[0]);
	}
	if (evt->num_params > evt->trailing)
	{
		evt->message = evt->params[evt->trailing];
	}
}

/*
 * Handles all events that do not (yet) have a dedicated event handler.
 */
static void
libtwirc_on_other(twirc_state_t *s, twirc_event_t *evt)
{
	// As we don't know what kind of event this is, we do nothing here
}

/*
 * This is not triggered by an actual IRC message, but by libtwirc once it 
 * detects that a connection to the IRC server has been established. Hence,
 * there is no twirc_event struct for this callback.
 */
static void
libtwirc_on_connect(twirc_state_t *s)
{
	// Set status to connected (discarding all other flags)
	s->status = TWIRC_STATUS_CONNECTED;

	// Request capabilities before login, so that we will receive the
	// GLOBALUSERSTATE command on login in addition to the 001 (WELCOME)
	libtwirc_capreq(s);

	// Start authentication process (user login)
	libtwirc_auth(s);
}

/*
 * This is not triggered by an actual IRC message, but by libtwirc once it 
 * detects that the connection to the IRC server has been lost. Hence, there
 * is no twirc_event struct for this callback.
 */
static void
libtwirc_on_disconnect(twirc_state_t *s)
{
	// Set status to disconnected (discarding all other flags)
	s->status = TWIRC_STATUS_DISCONNECTED;
	
	// Close the socket (this might fail as it might be closed already);
	// we're not checking for that error and therefore we don't report 
	// the error via s->error for two reasons: first, we kind of expect 
	// this to fail; second: we don't want to override more meaningful 
	// errors that might have occurred before 
	tcpsock_close(s->socket_fd);
}


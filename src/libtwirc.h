#ifndef LIBTWIRC_H
#define LIBTWIRC_H

// Name & Version
#define TWIRC_NAME "libtwirc"
#define TWIRC_VER_MAJOR 0
#define TWIRC_VER_MINOR 1
#define TWIRC_VER_BUILD 1

// Convenience
#define TWIRC_IPV4 TCPSNOB_IPV4
#define TWIRC_IPV6 TCPSNOB_IPV6

// State (bitfield)
#define TWIRC_STATUS_DISCONNECTED    0
#define TWIRC_STATUS_CONNECTING      1
#define TWIRC_STATUS_CONNECTED       2
#define TWIRC_STATUS_AUTHENTICATING  4
#define TWIRC_STATUS_AUTHENTICATED   8

// Errors
#define TWIRC_ERR_NONE               0
#define TWIRC_ERR_OUT_OF_MEMORY     -2
#define TWIRC_ERR_SOCKET_CREATE     -3
#define TWIRC_ERR_SOCKET_CONNECT    -4
#define TWIRC_ERR_SOCKET_SEND       -5
#define TWIRC_ERR_SOCKET_RECV       -6
#define TWIRC_ERR_SOCKET_CLOSE      -7
#define TWIRC_ERR_EPOLL_CREATE      -8
#define TWIRC_ERR_EPOLL_CTL         -9
#define TWIRC_ERR_EPOLL_WAIT       -10 // epoll_pwait() error
#define TWIRC_ERR_CONN_CLOSED      -11 // Connection lost: peer closed it
#define TWIRC_ERR_CONN_HANGUP      -12 // Connection lost: unexpectedly
#define TWIRC_ERR_CONN_SOCKET      -13 // Connection lost: socket error
#define TWIRC_ERR_EPOLL_SIG        -14 // epoll_pwait() caught a signal

// Maybe we should do this, too:
// https://github.com/shaoner/libircclient/blob/master/include/libirc_rfcnumeric.h
// Good source is:
// https://www.alien.net.au/irc/irc2numerics.html

// Message size needs to be large enough to accomodate a single IRC message 
// from the Twitch servers. Twitch limits the visible chat message part of 
// an IRC message to 512 bytes (510 without \r\n), but does not seem to take 
// tags, prefix, command or parameter length into account for the total length
// of the message, which can often result in messages that easily exceed the 
// 1024 bytes length limit as described by the IRCv3 spec. According to some 
// tests, we should be fine with doubling that to 2048. Note that the internal
// buffer of the twirc_state struct will use a buffer that is twice as big as
// TWIRC_MESSAGE_SIZE in order to be able to accomodate parts of an incomplete
// message in addition to a complete one.
#define TWIRC_MESSAGE_SIZE 2048

// The buffer size will be used for retrieving network data via recv(), which 
// means it could be very small (say, 256 bytes), as we call recv() in a loop
// until all data has been retrieved and processed. However, this will also 
// increase the CPU load required; having a larger buffer means we can process 
// more data in one go, which will improve speed. It seems sensible to choose 
// a size that is at least as large as the MESSAGE buffer (see above) so that
// we can assure that we will be able to process an entire message in one go.
#define TWIRC_BUFFER_SIZE TWIRC_MESSAGE_SIZE

// The prefix is an optional part of every IRC message retrieved from a server.
// As such, it can never exceed or even reach the size of a message itself.
// Usually, the prefix is a rather short string, based upon the length of the 
// user's nickname. 128 would probably do, but 256 seems to be a safer choice.
#define TWIRC_PREFIX_SIZE 256

// The command is always present, it tells us what kind of message we received.
// this could be PING or PRIVMSG or one of many possible numerical codes. 
// The page below lists known commands, the longest seems to be UNLOADMODULE or
// RELOADMODULE, both have a length of 12. Hence, a size of 16 seems enough.
// On the other hand, Twitch has custom commands, the longest of which is, at 
// the time of writing, GLOBALUSERSTATE, with a length of 16. Taking the null
// terminator into account, it seems safer to go with a generous 32 bytes here.
#define TWIRC_COMMAND_SIZE 32

// Total size for the PONG command, including its optional parameter.
// Currently, the command is always "PONG :tmi.twitch.tv", but it might change
// slightly in the future if Twitch is ever changing that URI. Currently, with
// the command taking up 20 bytes, 32 is more than enough of a buffer, but we
// might just go with twice that to be on the safe side.
#define TWIRC_PONG_SIZE 64

// The length of Twitch user names is limited to 25. Hence, 32 will do us fine.
// https://www.reddit.com/r/Twitch/comments/32w5b2/username_requirements/
#define TWIRC_NICK_SIZE 32

// The number of expected tags in an IRC message. This will be used to allocate 
// memory for the tags. If this number is smaller than the actual number of 
// tags in a message, realloc() will be used to allocate more memory. In other 
// words, to improve performance but keep memory footprint small, it would make 
// sense to choose this number slightly larger than the average number of tags 
// that can be expected in a message. Tests have shown that Twitch IRC usually 
// sends 13 tags per IRC message, so 16 seems to be a reasonable choice. 
#define TWIRC_NUM_TAGS 16

// The number of parameters is different depending on the type of the command.
// For most commands, two to three params will be sent, for some 4, for a few
// select commands even more. It seems reasonable to set this to 4, as it will
// cover most cases and only require realloc() in very rare cases, I believe.
// Conveniently, the maximum number of parameters is 15, leaving one for NULL.
// So we'll just realloc twice the size when we run out, going 4 to 8 to 16.
// http://www.networksorcery.com/enp/protocol/irc.htm
#define TWIRC_NUM_PARAMS 4

// If you want to connect to Twitch IRC anonymously, which means you'll be able
// to read chat but not participate, then you need to use the special username 
// "justinfan<randomnumber>", which seems to be a relic from the JustinTV days.
#define TWIRC_USER_ANON "justinfan"

// Defines the maximum number of digits that will be used as a suffix for the 
// anonymous username (TWIRC_USER_ANON)
#define TWIRC_USER_ANON_MAX_DIGITS 7

/*
 * Structures
 */

struct twirc_state;
struct twirc_event;
struct twirc_callbacks;
struct twirc_login;
struct twirc_tag;

typedef struct twirc_event twirc_event_t;
typedef struct twirc_login twirc_login_t;
typedef struct twirc_tag twirc_tag_t;
typedef struct twirc_state twirc_state_t;
typedef struct twirc_callbacks twirc_callbacks_t;

struct twirc_login
{
	char *host;
	char *port;
	char *nick;
	char *pass;
	char *name;
	char *id;
};

struct twirc_tag
{
	char *key;
	char *value;
};

struct twirc_event
{
	// Raw data
	char *raw;                         // The raw message as received
	// Separated raw data
	char *prefix;                      // IRC message prefix
	char *command;                     // IRC message command
	char **params;                     // IRC message parameter
	size_t num_params;                 // Number of elements in params
	int trailing;                      // Index of the trailing param
	twirc_tag_t **tags;                // IRC message tags
	size_t num_tags;                   // Number of elements in tags
	// For convenience
	char *origin;                      // Nick as extracted from prefix
	char *channel;                     // Channel as extracted from params
	char *target;                      // Target user of hosts, bans, etc.
	char *message;                     // Message as extracted from params
	char *ctcp;                        // CTCP commmand, if any
};

typedef void (*twirc_callback)(twirc_state_t *s, twirc_event_t *e);

struct twirc_callbacks
{
	twirc_callback connect;            // Connection established
	twirc_callback welcome;            // 001 received (logged in)
	twirc_callback globaluserstate;    // Logged in (+ user info)
	twirc_callback capack;             // Capabilities acknowledged
	twirc_callback ping;               // PING received
	twirc_callback join;               // User joined a channel
	twirc_callback part;               // User left a channel
	twirc_callback mode;               // User gained/lost mod status
	twirc_callback names;              // Reply to /NAMES command
	twirc_callback privmsg;            // Regular chat message in a channel
	twirc_callback whisper;            // Whisper (private message)
	twirc_callback action;             // CTCP ACTION received
	twirc_callback notice;             // Notice from server
	twirc_callback roomstate;          // Channel setting changed OR join
	twirc_callback usernotice;         // Sub, resub, giftsub, raid, ritual
	twirc_callback userstate;          // User joins or chats in channel (?)
	twirc_callback clearchat;          // Chat history purged or user banned
	twirc_callback clearmsg;           // A chat message has been removed
	twirc_callback hosttarget;         // Channel starts or stops host mode
	twirc_callback reconnect;          // Server is going for a restart soon
	twirc_callback disconnect;         // Connection interrupted
	twirc_callback invalidcmd;         // Server doesn't recognise command
	twirc_callback other;              // Everything else (for now)
	twirc_callback outbound;           // Messages we send TO the server
};

/*
 * Public functions
 */

// Initialization
twirc_state_t     *twirc_init();
twirc_callbacks_t *twirc_get_callbacks(twirc_state_t *s);

// Connecting and disconnecting
int twirc_connect(twirc_state_t *s, const char *host, const char *port, const char *nick, const char *pass);
int twirc_connect_anon(twirc_state_t *s, const char *host, const char *port);
int twirc_disconnect(twirc_state_t *s);

// Main flow control
int twirc_loop(twirc_state_t *s);
int twirc_tick(twirc_state_t *s, int timeout);

// Clean-up and shut-down
void twirc_kill(twirc_state_t *s);
void twirc_free(twirc_state_t *s);

// Retrieval of data from the twirc state
twirc_login_t *twirc_get_login(twirc_state_t *s);
twirc_tag_t   *twirc_get_tag_by_key(twirc_tag_t **tags, const char *key); // deprecated
twirc_tag_t   *twirc_get_tag(twirc_tag_t **tags, const char *key);
char const    *twirc_get_tag_value(twirc_tag_t **tags, const char *key);
int            twirc_get_last_error(const twirc_state_t *s);

// Twitc state status inforamtion
int twirc_is_connecting(const twirc_state_t *s);
int twirc_is_logging_in(const twirc_state_t *s);
int twirc_is_connected(const twirc_state_t *s);
int twirc_is_logged_in(const twirc_state_t *s);

// Custom user-data
void  twirc_set_context(twirc_state_t *s, void *ctx);
void *twirc_get_context(twirc_state_t *s);

// Twitch IRC commands
int twirc_cmd_raw(twirc_state_t *s, const char *msg);
int twirc_cmd_pass(twirc_state_t *s, const char *pass);
int twirc_cmd_nick(twirc_state_t *s, const char *nick);
int twirc_cmd_join(twirc_state_t *s, const char *chan);
int twirc_cmd_part(twirc_state_t *s, const char *chan);
int twirc_cmd_ping(twirc_state_t *s, const char *param);
int twirc_cmd_pong(twirc_state_t *s, const char *param);
int twirc_cmd_quit(twirc_state_t *s);
int twirc_cmd_privmsg(twirc_state_t *s, const char *chan, const char *msg);
int twirc_cmd_action(twirc_state_t *s, const char *chan, const char *msg);
int twirc_cmd_whisper(twirc_state_t *s, const char *nick, const char *msg);
int twirc_cmd_req_tags(twirc_state_t *s);
int twirc_cmd_req_membership(twirc_state_t *s);
int twirc_cmd_req_commands(twirc_state_t *s);
int twirc_cmd_mods(twirc_state_t *s, const char *chan);
int twirc_cmd_vips(twirc_state_t *s, const char *chan);
int twirc_cmd_color(twirc_state_t *s, const char *color);
int twirc_cmd_delete(twirc_state_t *s, const char *chan, const char *id);
int twirc_cmd_timeout(twirc_state_t *s, const char *chan, const char *nick, int secs, const char *reason);
int twirc_cmd_untimeout(twirc_state_t *s, const char *chan, const char *nick);
int twirc_cmd_ban(twirc_state_t *s, const char *chan, const char *nick, const char *reason);
int twirc_cmd_unban(twirc_state_t *s, const char *chan, const char *nick);
int twirc_cmd_slow(twirc_state_t *s, const char *chan, int secs);
int twirc_cmd_slowoff(twirc_state_t *s, const char *chan);
int twirc_cmd_followers(twirc_state_t *s, const char *chan, const char *time);
int twirc_cmd_followersoff(twirc_state_t *s, const char *chan);
int twirc_cmd_subscribers(twirc_state_t *s, const char *chan);
int twirc_cmd_subscribersoff(twirc_state_t *s, const char *chan);
int twirc_cmd_clear(twirc_state_t *s, const char *chan);
int twirc_cmd_r9k(twirc_state_t *s, const char *chan);
int twirc_cmd_r9koff(twirc_state_t *s, const char *chan);
int twirc_cmd_emoteonly(twirc_state_t *s, const char *chan);
int twirc_cmd_emoteonlyoff(twirc_state_t *s, const char *chan);
int twirc_cmd_commercial(twirc_state_t *s, const char *chan, int secs);
int twirc_cmd_host(twirc_state_t *s, const char *chan, const char *target);
int twirc_cmd_unhost(twirc_state_t *s, const char *chan);
int twirc_cmd_mod(twirc_state_t *s, const char *chan, const char *nick);
int twirc_cmd_unmod(twirc_state_t *s, const char *chan, const char *nick);
int twirc_cmd_vip(twirc_state_t *s, const char *chan, const char *nick);
int twirc_cmd_unvip(twirc_state_t *s, const char *chan, const char *nick);
int twirc_cmd_marker(twirc_state_t *s, const char *chan, const char *comment);

#endif

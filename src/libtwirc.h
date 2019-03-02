#ifndef LIBTWIRC_H
#define LIBTWIRC_H

// Name & Version
#define TWIRC_NAME "libtwirc"
#define TWIRC_VER_MAJOR 0
#define TWIRC_VER_MINOR 1
#ifdef BUILD
	#define TWIRC_VER_BUILD BUILD
#else
	#define TWIRC_VER_BUILD 0.0
#endif

// Convenience
#define TWIRC_IPV4 AF_INET
#define TWIRC_IPV6 AF_INET6

// State (bitfield)
#define TWIRC_STATUS_DISCONNECTED    0
#define TWIRC_STATUS_CONNECTING      1
#define TWIRC_STATUS_CONNECTED       2
#define TWIRC_STATUS_AUTHENTICATING  4
#define TWIRC_STATUS_AUTHENTICATED   8

// Errors
#define TWIRC_ERR_OUT_OF_MEMORY     -2
#define TWIRC_ERR_SOCKET_CREATE     -3
#define TWIRC_ERR_SOCKET_CONNECT    -4
#define TWIRC_ERR_SOCKET_SEND       -5
#define TWIRC_ERR_SOCKET_RECV       -6
#define TWIRC_ERR_SOCKET_CLOSE      -7
#define TWIRC_ERR_EPOLL_CREATE      -8
#define TWIRC_ERR_EPOLL_CTL         -9
#define TWIRC_ERR_EPOLL_WAIT       -10
#define TWIRC_ERR_CONN_CLOSED      -11 // Connection lost: peer closed it
#define TWIRC_ERR_CONN_HANGUP      -12 // Connection lost: unexpectedly
#define TWIRC_ERR_CONN_SOCKET      -13 // Connection lost: socket error

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
#define TWIRC_COMMAND_SIZE 16

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

// For some reason, when sending whispers, we apparently have to use a channel
// called #jtv - I don't get it, but it seems to work...
#define TWIRC_WHISPER_CHANNEL "#jtv"

struct twirc_state;

struct twirc_login
{
	char *host;
	char *port;
	char *nick;
	char *pass;
};

typedef struct twirc_login twirc_login_t;

struct twirc_user
{
	char *name;     // display-name
	char *id;       // user-id
	char color[8];  // color in chat
};

typedef struct twirc_user twirc_user_t;

struct twirc_tag
{
	char *key;
	char *value;
};

typedef struct twirc_tag twirc_tag_t;

// https://pastebin.com/qzzvpuB6
// TODO come up with a solution of how to hand the relevant meta data 
//      of certain events to the user (in the callbacks) without adding
//      a dozen function arguments; maybe use a twirc_tags struct that
//      contains one member for each possible tag? But there are plenty!
//      Alternatively, have two to three dedicated structs that will be
//      set or be NULL depending on the type of event? For example, one 
//      struct twirc_message_details, one struct twirc_user_details?
/*
struct twirc_tags
{
	char *badges;
	int ban_duration;
	int bits;
	char *broadcaster_lang;
	char *color;
	char *display_name;
	char *emotes;
	int emote_only;
	char *emote-sets;
	int followers_only;
	char *id;
	char *login;
	int mod;
	char *msg_id;
	// int msg_param_cumulative_months; -> msg_param_months
	char *msg_param_display_name;
	char *msg_param_login;
	int msg_param_months;
	char *msg_param_recipient_display_name;
	char *msg_param_recipient_id;
	char *msg_param_recipient_user_name;
	int msg_param_should_share_streak;
	int msg_param_streak_months;
	char *msg-param-sub-plan;
	char *msg-param-sub-plan-name;
	int msg_param_viewer_count;
	char *msg_param_ritual_name;
	int r9k;
	char *room_id;
	int slow;
	int subs_only;
	char *system_msg;
	char *target_msg_id;
	char *tmi_sent_ts;
	char *user_id;
};
*/

struct twirc_event
{
	// 'Raw' data
	char *prefix;                      // IRC message prefix
	char *command;                     // IRC message command
	char **params;                     // IRC message parameter
	size_t num_params;                 // Number of elements in params
	int trailing;                      // Index of the trailing param
	twirc_tag_t **tags;                // IRC message tags
	size_t num_tags;                   // Number of elements in tags
	
	// For convenience
	char *nick;                        // Nick as extracted from prefix
	char *channel;                     // Channel as extracted from params
	char *message;                     // Message as extracted from params
	char *ctcp;                        // CTCP commmand, if any
};

typedef struct twirc_event twirc_event_t;

typedef void (*twirc_callback)(struct twirc_state *s, struct twirc_event *e);

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
	twirc_callback clearchat;          // User was banned (temp or perm)
	twirc_callback clearmsg;           // A chat message has been removed
	twirc_callback hosttarget;         // Channel starts or stops host mode
	twirc_callback reconnect;          // Server is going for a restart soon
	twirc_callback disconnect;         // Connection interrupted
	twirc_callback invalidcmd;         // Server doesn't recognise command
	twirc_callback other;              // Everything else (for now)
};

typedef struct twirc_callbacks twirc_callbacks_t;

struct twirc_state
{
	int status : 8;                    // connection status
	int ip_type;                       // ip type, ipv4 or ipv6
	int socket_fd;                     // tcp socket file descriptor
	char *buffer;                      // irc message buffer
	twirc_login_t login;               // irc login data 
	twirc_user_t user;                 // twitch user details
	twirc_callbacks_t cbs;             // event callbacks
	int epfd;                          // epoll file descriptor
	int error;                         // last error that occured
	void *context;                     // pointer to user data
};

typedef struct twirc_state     twirc_state_t;

struct twirc_state *twirc_init();
struct twirc_callbacks *twirc_get_callbacks(twirc_state_t *s);

int twirc_connect(twirc_state_t *s, const char *host, const char *port, const char *nick, const char *pass);
int twirc_disconnect(twirc_state_t *s);
int twirc_send(twirc_state_t *s, const char *msg);
int twirc_recv(twirc_state_t *s, char *buf, size_t len);
int twirc_auth(twirc_state_t *s);
int twirc_kill(twirc_state_t *s);
int twirc_capreq(twirc_state_t *s);

void  twirc_set_context(twirc_state_t *s, void *ctx);
void *twirc_get_context(twirc_state_t *s);

char *twirc_tag_by_key(twirc_tag_t **tags, const char *key);

int twirc_loop(twirc_state_t *s, int timeout);
int twirc_tick(twirc_state_t *s, int timeout);

int twirc_cmd_pass(twirc_state_t *s, const char *pass);
int twirc_cmd_nick(twirc_state_t *s, const char *nick);
int twirc_cmd_join(twirc_state_t *s, const char *chan);
int twirc_cmd_part(twirc_state_t *s, const char *chan);
int twirc_cmd_privmsg(twirc_state_t *s, const char *chan, const char *msg);
int twirc_cmd_action(twirc_state_t *s, const char *chan, const char *msg);
int twirc_cmd_whisper(twirc_state_t *s, const char *nick, const char *msg);
int twirc_cmd_req_tags(twirc_state_t *s);
int twirc_cmd_req_membership(twirc_state_t *s);
int twirc_cmd_req_commands(twirc_state_t *s);
int twirc_cmd_pong(twirc_state_t *s, const char *param);
int twirc_cmd_quit(twirc_state_t *s);

int twirc_is_connecting(const twirc_state_t *s);
int twirc_is_logging_in(const twirc_state_t *s);
int twirc_is_connected(const twirc_state_t *s);
int twirc_is_logged_in(const twirc_state_t *s);

#endif

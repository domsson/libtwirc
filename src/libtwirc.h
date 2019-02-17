#ifndef LIBTWIRC_H
#define LIBTWIRC_H

#define TWIRC_NAME "libtwirc"
#define TWIRC_VER_MAJOR 0
#define TWIRC_VER_MINOR 1
#ifdef BUILD
	#define TWIRC_VER_BUILD BUILD
#else
	#define TWIRC_VER_BUILD 0.0
#endif

#define TWIRC_IPV4 AF_INET
#define TWIRC_IPV6 AF_INET6

#define TWIRC_STATUS_DISCONNECTED   0
#define TWIRC_STATUS_CONNECTING     1
#define TWIRC_STATUS_CONNECTED      2
#define TWIRC_STATUS_AUTHENTICATING 4
#define TWIRC_STATUS_AUTHENTICATED  8

// Message size needs to be large enough to accomodate a single IRC message 
// from the Twitch servers. Twitch limits the visible chat message part of 
// an IRC message to 512 bytes (510 without \r\n), but does not seem to take 
// tags, prefix, command or parameter length into account for the total length
// of the message, which can often result in messages that easily exceed the 
// 1024 bytes length limit as described by the IRCv3 spec. According to some 
// tests, we should be fine with doubling that to 2048.
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

// The length of nicknames (usernames) on Twitch is limited to 25.
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

struct twirc_state;

struct twirc_login
{
	char *host;
	char *port;
	char *nick;
	char *pass;
};

// <message> ::= ['@' <tags> <SPACE>] [':' <prefix> <SPACE> ] <command> <params> <crlf>
// Also, we might want to extract "nick" and "chan" from params, as these two will be in
// there pretty often - and most likely relevant for the user.
//
// (struct twirc_state *s, const char *cmd, const struct twirc_tag **tags, const char *prefix, const char **params) 
typedef void (*twirc_event)(struct twirc_state *s, const char *msg);

struct twirc_events
{
	twirc_event connect;
	twirc_event welcome;
	twirc_event message;
	twirc_event ping;
	twirc_event join;
	twirc_event part;
	twirc_event quit;
	twirc_event nick;
	twirc_event mode;
	twirc_event umode;
	twirc_event topic;
	twirc_event kick;
	twirc_event channel;
	twirc_event privmsg;
	twirc_event notice;
	twirc_event unknown;
};

struct twirc_state
{
	int status : 8;                 // connection status
	int running;                    // are we running in a loop?
	int ip_type;                    // ip type, ipv4 or ipv6
	int socket_fd;                  // tcp socket file descriptor
	char *buffer;                   // irc message buffer
	struct twirc_login login;       // irc login data 
	struct twirc_events events;     // event callbacks
	int epfd;                       // epoll file descriptor
};

struct twirc_tag
{
	char *key;
	char *value;
};

struct twirc_state* twirc_init(struct twirc_events *e);
void twirc_set_callbacks(struct twirc_state *s, struct twirc_events *e);

int twirc_connect(struct twirc_state *s, const char *host, const char *port, const char *pass, const char *nick);
int twirc_disconnect(struct twirc_state *s);
int twirc_send(struct twirc_state *s, const char *msg);
int twirc_recv(struct twirc_state *s, char *buf, size_t len);
int twirc_auth(struct twirc_state *s);
int twirc_kill(struct twirc_state *s);

int twirc_loop(struct twirc_state *s, int timeout);
int twirc_tick(struct twirc_state *s, int timeout);

int twirc_cmd_pass(struct twirc_state *s, const char *pass);
int twirc_cmd_nick(struct twirc_state *s, const char *nick);
int twirc_cmd_join(struct twirc_state *s, const char *chan);
int twirc_cmd_part(struct twirc_state *s, const char *chan);
int twirc_cmd_privmsg(struct twirc_state *s, const char *chan, const char *msg);
int twirc_cmd_pong(struct twirc_state *s);
int twirc_cmd_quit(struct twirc_state *s);


#endif

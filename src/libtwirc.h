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

#define TWIRC_BUFFER_SIZE  64 * 1024
#define TWIRC_PREFIX_SIZE  1024
#define TWIRC_MESSAGE_SIZE 2048
#define TWIRC_CHUNK_SIZE   4096
#define TWIRC_NUM_TAGS     16

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

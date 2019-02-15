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

#define TWIRC_BUFFER_SIZE 64 * 1024

struct twirc_state;
struct twirc_login;
struct twirc_events;

struct twirc_state* twirc_init(struct twirc_events *e);
void twirc_set_callbacks(struct twirc_state *s, struct twirc_events *e);

int twirc_connect(struct twirc_state *s, const char *host, const char *port, const char *pass, const char *nick);
int twirc_disconnect(struct twirc_state *s);
int twirc_send(struct twirc_state *s, const char *msg);
int twirc_recv(struct twirc_state *s, char *buf, size_t len);
int twirc_auth(struct twirc_state *s, const char *nick, const char *pass);
int twirc_kill(struct twirc_state *s);

int twirc_loop(struct twirc_state *s);
int twirc_tick(struct twirc_state *s, int timeout);

int twirc_cmd_pass(struct twirc_state *s, const char *pass);
int twirc_cmd_nick(struct twirc_state *s, const char *nick);
int twirc_cmd_join(struct twirc_state *s, const char *chan);
int twirc_cmd_part(struct twirc_state *s, const char *chan);
int twirc_cmd_privmsg(struct twirc_state *s, const char *chan, const char *msg);
int twirc_cmd_pong(struct twirc_state *s);
int twirc_cmd_quit(struct twirc_state *s);


#endif

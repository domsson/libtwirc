#ifndef LIBTWIRC_INTERNAL_H
#define LIBTWIRC_INTERNAL_H

#include "libtwirc.h"

/*
 * Structures
 */

struct twirc_state
{
	int status : 8;                    // Connection/login status
	int ip_type;                       // IP type, IPv4 or IPv6
	int socket_fd;                     // TCP socket file descriptor
	char *buffer;                      // IRC message buffer
	twirc_login_t login;               // IRC login data 
	twirc_callbacks_t cbs;             // Event callbacks
	int epfd;                          // epoll file descriptor
	int error;                         // Last error that occured
	void *context;                     // Pointer to user data
};

/*
 * Private functions
 */

static int libtwirc_send(twirc_state_t *s, const char *msg);
static int libtwirc_recv(twirc_state_t *s, char *buf, size_t len);
static int libtwirc_auth(twirc_state_t *s);
static int libtwirc_capreq(twirc_state_t *s);

#endif

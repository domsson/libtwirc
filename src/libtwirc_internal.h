#ifndef LIBTWIRC_INTERNAL_H
#define LIBTWIRC_INTERNAL_H

#include "libtwirc.h"

/*
 * Structures
 */

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

int libtwirc_send(twirc_state_t *s, const char *msg);
int libtwirc_recv(twirc_state_t *s, char *buf, size_t len);
int libtwirc_auth(twirc_state_t *s);
int libtwirc_capreq(twirc_state_t *s);

#endif

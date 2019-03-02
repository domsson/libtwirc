#include <stdio.h>      // NULL, fprintf(), perror()
#include "libtwirc.h"

/*
 * Returns 1 if state is currently connecting to Twitch IRC, otherwise 0.
 */
int twirc_is_connecting(const struct twirc_state *state)
{
	return state->status & TWIRC_STATUS_CONNECTING ? 1 : 0;
}

/*
 * Returns 1 if state is connected to Twitch IRC, otherwise 0.
 */
int twirc_is_connected(const struct twirc_state *state)
{
	return state->status & TWIRC_STATUS_CONNECTED ? 1 : 0;
}

/*
 * Returns 1 if state is currently authenticating, otherwise 0.
 */
int twirc_is_logging_in(const struct twirc_state *state)
{
	return state->status & TWIRC_STATUS_AUTHENTICATING ? 1 : 0;
}

/*
 * Returns 1 if state is authenticated (logged in), otherwise 0.
 */
int twirc_is_logged_in(const struct twirc_state *state)
{
	return state->status & TWIRC_STATUS_AUTHENTICATED ? 1 : 0;
}

char *twirc_tag_by_key(struct twirc_tag **tags, const char *key)
{
	for (int i = 0; tags[i] != NULL; ++i)
	{
		if (strcmp(tags[i]->key, key) == 0)
		{
			return tags[i]->value;
		}
	}
	return NULL;
}


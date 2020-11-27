#include <stdio.h>      // NULL, fprintf(), perror()
#include "libtwirc.h"

/*
 * Returns 1 if state is currently connecting to Twitch IRC, otherwise 0.
 */
int
twirc_is_connecting(const twirc_state_t *state)
{
	return state->status & TWIRC_STATUS_CONNECTING ? 1 : 0;
}

/*
 * Returns 1 if state is connected to Twitch IRC, otherwise 0.
 */
int
twirc_is_connected(const twirc_state_t *state)
{
	return state->status & TWIRC_STATUS_CONNECTED ? 1 : 0;
}

/*
 * Returns 1 if state is currently authenticating, otherwise 0.
 */
int
twirc_is_logging_in(const twirc_state_t *state)
{
	return state->status & TWIRC_STATUS_AUTHENTICATING ? 1 : 0;
}

/*
 * Returns 1 if state is authenticated (logged in), otherwise 0.
 */
int
twirc_is_logged_in(const twirc_state_t *state)
{
	return state->status & TWIRC_STATUS_AUTHENTICATED ? 1 : 0;
}

/*
 * Return the login struct, which contains login and user data.
 */
twirc_login_t*
twirc_get_login(twirc_state_t *state)
{
	return &state->login;
}

/*
 * Searches the provided array of twirc_tag structs for a tag with the 
 * provided key, then returns a pointer to that tag. If no tag with the
 * given key was found, NULL will be returned.
 */
twirc_tag_t*
twirc_get_tag(twirc_tag_t **tags, const char *key)
{
	for (int i = 0; tags[i] != NULL; ++i)
	{
		if (strcmp(tags[i]->key, key) == 0)
		{
			return tags[i];
		}
	}
	return NULL;
}

/*
 * Deprecated alias of twirc_get_tag(), use that instead.
 */
twirc_tag_t*
twirc_get_tag_by_key(twirc_tag_t **tags, const char *key)
{
	return twirc_get_tag(tags, key);
}

/*
 * Searches the provided array of twirc_tag structs for a tag with the 
 * provided key, then returns a pointer to that tag's value. If no tag
 * with the given key was found, NULL will be returned.
 */
char const*
twirc_get_tag_value(twirc_tag_t **tags, const char *key)
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

/*
 * Return the error code of the last error or -1 if non occurred so far.
 */
int
twirc_get_last_error(const twirc_state_t *state)
{
	return state->error;
}

void
twirc_set_context(twirc_state_t *s, void *ctx)
{
	s->context = ctx;
}

void
*twirc_get_context(twirc_state_t *s)
{
	return s->context;
}

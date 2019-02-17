#include <stdio.h>      // NULL, fprintf(), perror()
#include <stdlib.h>     // NULL, EXIT_FAILURE, EXIT_SUCCESS
#include <string.h>     //
#include <errno.h>      // errno
#include <sys/types.h>  // ssize_t
#include "libtwirc.h"

/*
 *
 */
int read_token(char *buf, size_t len)
{
	FILE *fp;
	fp = fopen ("token", "r");
	if (fp == NULL)
	{
		return 0;
	}
	char *res = fgets(buf, len, fp);
	if (res == NULL)
	{
		fclose(fp);
		return 0;
	}
	size_t res_len = strlen(buf);
	if (buf[res_len-1] == '\n')
	{
		buf[res_len-1] = '\0';
	}
	fclose (fp);
	return 1;
}

/*
 *
 */
void handle_connect(struct twirc_state *state, const char *msg)
{
	fprintf(stderr, "handle_connect()\n");
	twirc_cmd_join(state, "#domsson");
}

void handle_welcome(struct twirc_state *state, const char *msg)
{
	fprintf(stderr, "handle_welcome()\n");
	twirc_send(state, "CAP REQ :twitch.tv/tags");
	twirc_cmd_join(state, "#domsson");
}

/*
 *
 */
void handle_join(struct twirc_state *state, const char *msg)
{
	fprintf(stderr, "handle_join()\n");
	if (strstr(msg, "kaulmate!kaulmate@kaulmate.tmi.twitch.tv") != NULL)
	{
		twirc_cmd_privmsg(state, "#domsson", "jobruce is the best!");
	}
}

/*
 * main
 */
int main(void)
{
	// HELLO WORLD
	fprintf(stderr, "Starting up %s version %o.%o build %f\n",
		TWIRC_NAME, TWIRC_VER_MAJOR, TWIRC_VER_MINOR, TWIRC_VER_BUILD);

	// SET UP CALLBACKS
	struct twirc_events e = { 0 };
	e.connect = handle_connect;
	e.welcome = handle_welcome;
	e.join = handle_join;

	// CREATE TWIRC INSTANCE
	struct twirc_state *s = twirc_init(&e);
	if (s == NULL)
	{
		fprintf(stderr, "Could not init twirc state\n");
		return EXIT_FAILURE;
	}

	fprintf(stderr, "Successfully initialized twirc state...\n");
	
	// READ IN TOKEN FILE
	char token[128];
	int token_success = read_token(token, 128);
	if (token_success == 0)
	{
		fprintf(stderr, "Could not read token file\n");
		return EXIT_FAILURE;
	}

	// CONNECT TO THE IRC SERVER
	if (twirc_connect(s, "irc.chat.twitch.tv", "6667", token, "kaulmate") != 0)
	{
		fprintf(stderr, "Could not connect socket\n");
		return EXIT_FAILURE;
	}

	if (errno = EINPROGRESS)
	{
		fprintf(stderr, "Connection initiated...\n");
	}

	// MAIN LOOP
	twirc_loop(s, 1000);

	twirc_kill(s);
	fprintf(stderr, "Bye!\n");

	return EXIT_SUCCESS;
}


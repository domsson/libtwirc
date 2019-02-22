#include <stdio.h>      // NULL, fprintf(), perror()
#include <stdlib.h>     // NULL, EXIT_FAILURE, EXIT_SUCCESS
#include <string.h>     //
#include <errno.h>      // errno
#include <sys/types.h>  // ssize_t
#include <time.h>
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

void handle_ping(struct twirc_state *state, const struct twirc_message *msg)
{
	fprintf(stdout, "*** received PING: %s\n", msg->params[0]);
}

/*
 *
 */
void handle_connect(struct twirc_state *state, const struct twirc_message *msg)
{
	fprintf(stdout, "*** connected!\n");
}

/*
 *
 */
void handle_welcome(struct twirc_state *state, const struct twirc_message *msg)
{
	// Let's join a lot of channels to test this bad boy!
	//twirc_cmd_join(state, "#domsson");
/*
	twirc_cmd_join(state, "#hanryang1125");
	twirc_cmd_join(state, "#toborprime");
	twirc_cmd_join(state, "#honestdangames");
	twirc_cmd_join(state, "#meowko");
	twirc_cmd_join(state, "#kitboga");
	twirc_cmd_join(state, "#hyubsama");
	twirc_cmd_join(state, "#bawnsai");
	twirc_cmd_join(state, "#bouphe");
	twirc_cmd_join(state, "#retrogaijin");
	twirc_cmd_join(state, "#yumyumyu77");
	twirc_cmd_join(state, "#esl_csgo");
*/
}

/*
 *
 */
void handle_join(struct twirc_state *state, const struct twirc_message *msg)
{
	fprintf(stdout, "*** joined %s\n", msg->params[0]);
	if (strcmp(msg->prefix, "kaulmate!kaulmate@kaulmate.tmi.twitch.tv") == 0
		&& strcmp(msg->params[0], "#domsson") == 0)
	{
		twirc_cmd_privmsg(state, "#domsson", "jobruce is the best!");
	}
}

void handle_privmsg(struct twirc_state *state, const struct twirc_message *msg)
{
	time_t t = time(NULL);
	struct tm tm = *localtime(&t);
	
	fprintf(stdout, "[%02d:%02d:%02d] (%s) %s: %s\n", tm.tm_hour, tm.tm_min, tm.tm_sec, msg->channel, msg->nick, msg->params[1]);
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
	e.join    = handle_join;
	e.privmsg = handle_privmsg;

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


#include <stdio.h>      // NULL, fprintf(), perror()
#include <stdlib.h>     // NULL, EXIT_FAILURE, EXIT_SUCCESS
#include <string.h>     //
#include <errno.h>      // errno
#include <sys/types.h>  // ssize_t
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include "libtwirc.h"

static volatile int running; // Used to stop main loop in case of SIGINT etc
static volatile int handled; // The last signal that has been handled

/*
 * Read a file called 'token' (in the same directory as the code is run)
 * and read it into the buffer pointed to by buf. The file is exptected to
 * contain one line, and one line only: the Twitch oauth token, which should
 * begin with "oauth:". This is used as your IRC password on Twitch IRC.
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
void handle_connect(struct twirc_state *s, struct twirc_event *evt)
{
	fprintf(stdout, "*** connected!\n");
}

/*
 *
 */
void handle_welcome(struct twirc_state *s, struct twirc_event *evt)
{
	fprintf(stdout, "*** logged in!\n");
}


void handle_disconnect(struct twirc_state *s, struct twirc_event *evt)
{
	fprintf(stdout, "*** connection lost\n");
}

void handle_everything(struct twirc_state *s, struct twirc_event *evt)
{
	fprintf(stdout, "> %s\n", evt->raw);
	if (evt->target)
	{
		fprintf(stdout, "  '--> target = %s\n", evt->target);
	}
}

void handle_outgoing(struct twirc_state *s, struct twirc_event *evt)
{
	if (strcmp(evt->command, "PASS") == 0)
	{
		fprintf(stdout, "< PASS ********\n");
	}
	else
	{
		fprintf(stdout, "< %s\n", evt->raw);
	}
}

void sigint_handler(int sig)
{
	fprintf(stderr, "*** received signal, exiting\n");
	running = 0;
	handled = sig;
}

void *input_thread(void *vargp)
{
	struct twirc_state *s = (struct twirc_state*) vargp;

	fprintf(stderr, "*** input thread launched\n");
	char buf[2048];
	while (running)
	{
		fgets(buf, 2048, stdin);
		twirc_send(s, buf);
	}
	return NULL;
}

/*
 * main
 */
int main(void)
{
	// HELLO WORLD
	fprintf(stderr, "Starting up %s version %o.%o build %f\n",
		TWIRC_NAME, TWIRC_VER_MAJOR, TWIRC_VER_MINOR, TWIRC_VER_BUILD);

	// Make sure we still do clean-up on SIGINT (ctrl+c)
	// and similar signals that indicate we should quit.
	struct sigaction sa_int = {
		.sa_handler = &sigint_handler
	};
	if (sigaction(SIGINT, &sa_int, NULL) == -1)
	{
		fprintf(stderr, "Failed to register SIGINT handler\n");
	}
	if (sigaction(SIGQUIT, &sa_int, NULL) == -1)
	{
		fprintf(stderr, "Failed to register SIGQUIT handler\n");
	}
	if (sigaction (SIGTERM, &sa_int, NULL) == -1)
	{
		fprintf(stderr, "Failed to register SIGTERM handler\n");
	}

	// CREATE TWIRC INSTANCE
	struct twirc_state *s = twirc_init();
	
	// SET UP CALLBACKS
	struct twirc_callbacks *cbs = twirc_get_callbacks(s);
	cbs->connect         = handle_connect;
	cbs->welcome         = handle_welcome;
	cbs->globaluserstate = handle_everything;
	cbs->capack          = handle_everything;
	cbs->ping            = handle_everything;
	cbs->join            = handle_everything;
	cbs->part            = handle_everything;
	cbs->mode            = handle_everything;
	cbs->names           = handle_everything;
	cbs->privmsg         = handle_everything;
	cbs->whisper         = handle_everything;
	cbs->action          = handle_everything;
	cbs->notice          = handle_everything;
	cbs->roomstate       = handle_everything;
	cbs->usernotice      = handle_everything;
	cbs->userstate       = handle_everything;
	cbs->clearchat       = handle_everything;
	cbs->clearmsg        = handle_everything;
	cbs->hosttarget      = handle_everything;
	cbs->reconnect       = handle_everything;
	cbs->invalidcmd      = handle_everything;
	cbs->other           = handle_everything;
	cbs->disconnect      = handle_disconnect;
	cbs->outgoing        = handle_outgoing;

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
	if (twirc_connect(s, "irc.chat.twitch.tv", "6667", "kaulmate", token) != 0)
	{
		fprintf(stderr, "Could not connect socket\n");
		return EXIT_FAILURE;
	}

	fprintf(stderr, "Connection initiated...\n");

	pthread_t t;
	pthread_create(&t, NULL, &input_thread, (void *) s);

	// MAIN LOOP
	//twirc_loop(s, 1000);
	running = 1;
	while (twirc_tick(s, 1000) == 0 && running == 1)
	{
	}

	twirc_kill(s);
	fprintf(stderr, "Bye!\n");

	return EXIT_SUCCESS;
}


#include <stdlib.h>     // NULL, malloc, free, realloc
#include <string.h>     // strcmp, strlen, strdup, strstr
#include "libtwirc.h"

/*
 * Takes an escaped string (as described in the IRCv3 spec, section tags)
 * and returns a pointer to a malloc'd string that holds the unescaped string.
 * Remember that the returned pointer has to be free'd by the caller!
 */
char *libtwirc_unescape(const char *str)
{
	size_t str_len = strlen(str);
	char *unescaped = malloc((str_len + 1) * sizeof(char));

	int u = 0;
	for (int i = 0; i < (int) str_len; ++i)
	{
		if (str[i] == '\\')
		{
			if (str[i+1] == ':') // "\:" -> ";"
			{
				unescaped[u++] = ';';
				++i;
				continue;
			}
			if (str[i+1] == 's') // "\s" -> " ";
			{
				unescaped[u++] = ' ';
				++i;
				continue;
			}
			if (str[i+1] == '\\') // "\\" -> "\";
			{
				unescaped[u++] = '\\';
				++i;
				continue;
			}
			if (str[i+1] == 'r') // "\r" -> '\r' (CR)
			{
				unescaped[u++] = '\r';
				++i;
				continue;
			}
			if (str[i+1] == 'n') // "\n" -> '\n' (LF)
			{
				unescaped[u++] = '\n';
				++i;
				continue;
			}
		}
		unescaped[u++] = str[i];
	}
	unescaped[u] = '\0';
	return unescaped;
}

struct twirc_tag *libtwirc_create_tag(const char *key, const char *val)
{
	struct twirc_tag *tag = malloc(sizeof(struct twirc_tag));
	tag->key   = strdup(key);
	tag->value = val == NULL ? NULL : libtwirc_unescape(val);
	return tag;
}

void libtwirc_free_tag(struct twirc_tag *tag)
{
	free(tag->key);
	free(tag->value);
	free(tag);
	tag = NULL;
}

void libtwirc_free_tags(struct twirc_tag **tags)
{
	if (tags == NULL)
	{
		return;
	}
	for (int i = 0; tags[i] != NULL; ++i)
	{
		libtwirc_free_tag(tags[i]);
	}
	free(tags);
	tags = NULL;
}

/*
 * Extracts tags from the beginning of an IRC message, if any, and returns them
 * as a pointer to a dynamically allocated array of twirc_tag structs, where 
 * each struct contains two members, key and value, representing the key and 
 * value of a tag, respectively. The value member of a tag can be NULL for tags 
 * that are key-only. The last element of the array will be a NULL pointer, so 
 * you can loop over all tags until you hit NULL. The number of extracted tags
 * is returned in len. If no tags have been found at the beginning of msg, tags
 * will be NULL, len will be 0 and this function will return a pointer to msg.
 * Otherwise, a pointer to the part of msg after the tags will be returned. 
 *
 * https://ircv3.net/specs/core/message-tags-3.2.html
 */
const char *libtwirc_parse_tags(const char *msg, struct twirc_tag ***tags, size_t *len)
{
	// If msg doesn't start with "@", then there are no tags
	if (msg[0] != '@')
	{
		*len = 0;
		*tags = NULL;
		return msg;
	}

	// Find the next space (the end of the tags string within msg)
	char *next = strstr(msg, " ");
	
	// Duplicate the string from after the '@' until the next space
	char *tag_str = strndup(msg + 1, next - (msg + 1));

	// Set the initial number of tags we want to allocate memory for
	size_t num_tags = TWIRC_NUM_TAGS;
	
	// Allocate memory in the provided pointer to ptr-to-array-of-structs
	*tags = malloc(num_tags * sizeof(struct twirc_tag*));

	char *tag;
	int i;
	for (i = 0; (tag = strtok(i == 0 ? tag_str : NULL, ";")) != NULL; ++i)
	{
		// Make sure we have enough space; last element has to be NULL
		if (i >= num_tags - 1)
		{
			size_t add = num_tags * 0.5;
			num_tags += add;
			*tags = realloc(*tags, num_tags * sizeof(struct twirc_tag*));
		}

		char *eq = strstr(tag, "=");

		// It's a key-only tag, like "foo" (never seen that Twitch)
		// Hence, we didn't find a '=' at all 
		if (eq == NULL)
		{
			(*tags)[i] = libtwirc_create_tag(tag, NULL);
		}
		// It's either a key-only tag with a trailing '=' ("foo=")
		// or a tag with key-value pair, like "foo=bar"
		else
		{
			// Turn the '=' into '\0' to separate key and value
			eq[0] = '\0'; // Turn the '=' into '\0'
			// Set val to NULL for key-only tags (to avoid "")
			char *val = eq[1] == '\0' ? NULL : eq+1;
			(*tags)[i] = libtwirc_create_tag(tag, val);
		}

		//fprintf(stderr, ">>> TAG %d: %s = %s\n", i, (*tags)[i]->key, (*tags)[i]->value);
	}

	// Set the number of tags found
	*len = i;

	free(tag_str);
	
	// Trim this down to the exact right size
	if (i < num_tags - 1)
	{
		*tags = realloc(*tags, (i + 1) * sizeof(struct twirc_tag*));
	}

	// Make sure the last element is a NULL ptr
	(*tags)[i] = NULL;

	// Return a pointer to the remaining part of msg
	return next + 1;
}

/*
 * Searches the provided array of twirc_tag structs for a tag with the 
 * provided key, then returns a pointer to that tag's value.
 */
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

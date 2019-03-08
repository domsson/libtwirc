#include <assert.h>     // assert
#include <stdlib.h>     // NULL, malloc, free
#include <string.h>     // strcmp, strlen, strstr, memcpy
#include "libtwirc.h"

#ifdef TWIRC_TAGS_DEBUG
#undef TWIRC_TAGS_DEBUG
#include <stdio.h>
#define TWIRC_TAGS_DEBUG(...) fprintf(stderr, __VA_ARGS__)
#else
#define TWIRC_TAGS_DEBUG(...)
#endif // TWIRC_TAGS_DEBUG

/*
 * Takes an escaped string (as described in the IRCv3 spec, section tags)
 * and returns a pointer to a malloc'd string that holds the unescaped string.
 * Remember that the returned pointer has to be free'd by the caller!
 */
void libtwirc_unescape_tag_value_in_situ(char *escaped)
{
	assert(escaped != NULL && "Must provide a message to escape");
	
	char *unescaped = escaped;

	for(; *escaped; ++escaped)
	{
		if (*escaped != '\\')
		{
			*unescaped++ = *escaped;
		}
		else
		{
			switch(escaped[1]) // check next character
			{
				case '\\': *unescaped++ = '\\'; break; // "\\" -> "\"
				case 'r':  *unescaped++ = '\r'; break; // "\r" -> '\r' (CR)
				case 'n':  *unescaped++ = '\n'; break; // "\n" -> '\n' (LF)
				case 's':  *unescaped++ = ' ';  break; // "\:" -> ";"
				case ':':  *unescaped++ = ';';  break; // "\:" -> ";"
				default: *unescaped++ = '\\'; continue; // unknown escape sequence; take \ verbatim and skip additional increment
			}
			++escaped;
		}
	}
	*unescaped = '\0';
}

void libtwirc_free_tags(struct twirc_tag **tags)
{
	free(tags);
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
	assert(msg != NULL && "Must provide a message");
	assert(tags != NULL && "Must provide a storage target for tags");
	// If msg doesn't start with "@", then there are no tags
	if (msg[0] != '@')
	{
		if (len) { *len = 0; }
		*tags = NULL;
		return msg;
	}

	// Find the next space (the end of the tags string within msg)
	const char * const next = strstr(msg, " ");
	
	// Count the number of tags
	size_t num_tags = 1;
	for(const char *it = msg; it != next; ++it)
	{
		if (*it == ';') { ++num_tags; }
	}
	
	// Allocate a single buffer for the whole tag structure (less allocations and more cache locality)
	struct twirc_tag ** const tag_ptr_array = malloc(
		(num_tags+1) * (sizeof(struct twirc_tag *) + sizeof(struct twirc_tag)) // twitch_tag*[num_tags+1] and twitch_tag[num_tags+1]
		+ (next - (msg+1)) // strdup of tags (without '@' prefix)
		+ 1 // '\0' delimiter for tag strdup
	);
	struct twirc_tag * const tag_array = (struct twirc_tag*)(tag_ptr_array + num_tags + 1);
	char * const tag_strdup = (char*)(tag_array + num_tags + 1);
	char * const tag_strdup_end = tag_strdup + (next - (msg+1));
	
	// Create our own little tag string
	memcpy(tag_strdup, msg+1, (next - msg) - 1);
	*tag_strdup_end = '\0';
	
	TWIRC_TAGS_DEBUG("'%s'\n>'%s'\n", msg, tag_strdup);
	
	char *tag_start = tag_strdup;
	struct twirc_tag **current_tag_ptr = tag_ptr_array;
	struct twirc_tag *current_tag = tag_array;
	while(tag_start != tag_strdup_end)
	{
		*current_tag_ptr = current_tag;
		
		char *next_tag_start = strstr(tag_start, ";");
		if (next_tag_start == NULL)
		{
			next_tag_start = tag_strdup_end;
		}
		else
		{
			*next_tag_start = '\0';
			++next_tag_start;
		}
		
		TWIRC_TAGS_DEBUG("#%d: '%s'\n", current_tag - tag_array, tag_start);
		
		char * value_start = strstr(tag_start, "=");
		if (value_start == NULL)
		{
			TWIRC_TAGS_DEBUG("\t '%s' -> (no value)\n", tag_start);
		}
		else
		{
			*value_start++ = '\0';
			libtwirc_unescape_tag_value_in_situ(value_start);
			TWIRC_TAGS_DEBUG("\t '%s' -> '%s'\n", tag_start, value_start);
		}
		
		current_tag->key = tag_start;
		current_tag->value = value_start;
		
		tag_start = next_tag_start;
		++current_tag_ptr;
		++current_tag;
	}
	
	assert(current_tag_ptr == tag_ptr_array + num_tags && "tag pointer index does not point to expected end tag");
	assert(current_tag == tag_array + num_tags && "tag index does not point to expected end tag");
	
	// Fill tag sentinels
	*current_tag_ptr = NULL;
	current_tag->key = NULL;
	current_tag->value = NULL;
	
	// Set output parameters
	if (len) { *len = num_tags; }
	*tags = tag_ptr_array;

	// Return a pointer to the remaining part of msg
	return next + 1;
}

/**
 * Special value to be returned by twirc_tag_by_key if the
 * key exists but has no value attached.
 */
char *TWIRC_VALUELESS_TAG = "<valueless tag>";

/*
 * Searches the provided array of twirc_tag structs for a tag with the 
 * provided key.
 * \return
 *     - \c NULL if no tag with the given key was found,
 *     - the value of the tag, if a tag was found and has a value,
 *     - \c TWIRC_VALUELESS_TAG, if a tag was found, but has no value.
 */
char *twirc_tag_by_key(struct twirc_tag **tags, const char *key)
{
	for (struct twirc_tag **it = tags; *it != NULL; ++it)
	{
		if (strcmp((*it)->key, key) == 0)
		{
			return (*it)->value ? (*it)->value : TWIRC_VALUELESS_TAG;
		}
	}
	return NULL;
}

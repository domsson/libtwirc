#include <stdio.h>
#define TWIRC_TAGS_DEBUG
#include "../src/libtwirc_tags.c"

void key_lookup(struct twirc_tag **tags, const char *key) {
	char *result = twirc_tag_by_key(tags, key);
	if (result == NULL) {
		printf("Lookup '%s' by name: Tag not found.\n", key);
	}
	else if (result == TWIRC_VALUELESS_TAG) {
		printf("Lookup '%s' by name: Tag exists, but has no value.\n", key);
	}
	else {
		printf("Lookup '%s' by name: Tag with value: '%s'.\n", key, result);
	}
}

int main() {
	struct twirc_tag **tags = NULL;
	size_t len = -1;
	
	const char * const message = "@foo=bar;valueless;=nameless;;bar=foo PRIVMSG #foobar :Hello World!";
	
	const char * remainder = libtwirc_parse_tags(message, &tags, &len);
	
	printf("-----------------------------------\n");
	
	for(struct twirc_tag **it = tags; *it != NULL; ++it) {
		if ((*it)->value == NULL) {
			printf("'%s' => (no value)\n", (*it)->key);
		}
		else {
			printf("'%s' => '%s'\n", (*it)->key, (*it)->value);
		}
	}
	
	printf("-----------------------------------\n");

	key_lookup(tags, "bar");
	key_lookup(tags, "valueless");
	key_lookup(tags, "");
	key_lookup(tags, "nonexistant");

	libtwirc_free_tags(tags);
}

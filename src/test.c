#include <stdio.h>
#include <string.h>


void chopN(char *str, size_t n)
{
	if (n == 0 || str == 0) {
		return;
	}

	size_t len = strlen(str);
	
	if (n > len)
	{
		return;  // Or: n = len;
	}
	
	memmove(str, str+n, len - n + 1);
}

size_t shift_cmd(char *dest, char *src) 
{
	char *crlf = strstr(src, "\r\n");
	if (crlf == NULL)
	{
		return 0;
	}

	size_t src_len = strlen(src);
	size_t end_len = strlen(crlf);
	size_t cmd_len = src_len - end_len;

	strncpy(dest, src, cmd_len);
	dest[cmd_len] = '\0';

	//chopN(src, cmd_len+2);
	memmove(src, crlf + 2, end_len - 2);
	src[end_len - 2] = '\0';
}

int main()
{
	char foo[] = "command no. 1\r\nnext command\r\nincomplete com";
	printf("foo:\n%s\n\n", foo);
	char bar[128];
	bar[0] = '\0';
	shift_cmd(bar, foo);
	shift_cmd(bar, foo);
	shift_cmd(bar, foo);
	//chopN(foo, 15);
	printf("foo:\n%s\n\n", foo);
	printf("bar:\n%s\n\n", bar);
	return 0;
}

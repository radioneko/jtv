#include <sys/socket.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <pthread.h>
#include "jtv_memory.h"
#include "jtv_chat.h"

static const char *nick_colors[] = {
#if 0
	"\033[01;31m",
	"\033[01;32m",
	"\033[01;33m",
	"\033[01;34m",
	"\033[01;35m",
	"\033[01;36m",
	"\033[01;37m"
#else
#	include "xterm256.h"
#endif
};

static const char *lcc_normal = "\033[00m";

struct jtv_chat {
	int					jc_sock;
	unsigned			jc_buf_len;
	const char			*jc_channel;
	struct sockaddr_in	jc_host;
	char				jc_buf[8192];
	pthread_t			jc_thr;
};

static struct jtv_chat*
jtv_chat_new(const char *host, unsigned port, const char *channel)
{
	unsigned len;
	struct jtv_chat *chat = NULL;
	struct hostent *he;

	len = strlen(channel) + 1;
	chat = xmalloc(sizeof(*chat) + len);
	memcpy(chat + 1, channel, len);
	chat->jc_channel = (const char*)(chat + 1);

	/* Resolve hostname */
	he = gethostbyname(host);
	if (!he)
		goto failure;
	chat->jc_host.sin_family = AF_INET;
	chat->jc_host.sin_addr = *(struct in_addr*)he->h_addr;
	chat->jc_host.sin_port = htons(port);

	chat->jc_sock = -1;
	chat->jc_buf_len = 0;

	return chat;

failure:
	if (chat)
		free(chat);
	return NULL;
}

static bool
jtv_chat_connect(struct jtv_chat* chat)
{
	char data[1024];
	unsigned pos = 0, len;

	if (chat->jc_sock != -1) {
		close(chat->jc_sock);
		chat->jc_sock = -1;
	}

	chat->jc_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (chat->jc_sock == -1)
		return false;
	if (connect(chat->jc_sock, (struct sockaddr*)&chat->jc_host, sizeof(chat->jc_host)) == -1) {
		int en = errno;
		chat->jc_sock = -1;
		close(chat->jc_sock);
		errno = en;
		return false;
	}

	/* Send initial sequence */
	pos = sprintf(data, "TWITCHCLIENT 2\r\n");
	data[pos++] = 0; data[pos++] = 0;
	pos += sprintf(data + pos, "PASS blah\r\n");
	data[pos++] = 0; data[pos++] = 0;
	pos += snprintf(data + pos, sizeof(data) - pos, "NICK justinfan%u\r\n", (unsigned)(random() % 100000 + 10000));
	data[pos++] = 0; data[pos++] = 0;
	pos += snprintf(data + pos, sizeof(data) - pos, "JOIN #%s\r\n", chat->jc_channel);
	data[pos++] = 0; data[pos++] = 0;
	pos += snprintf(data + pos, sizeof(data) - pos, "JTVROOMS #%s\r\n", chat->jc_channel);
	data[pos++] = 0; data[pos++] = 0;
	len = pos;
	pos = 0;

	do {
		ssize_t bytes = write(chat->jc_sock, data + pos, len - pos);
		if (bytes == -1 && errno == EINTR)
			continue;
		if (bytes == -1) {
			int en = errno;
			close(chat->jc_sock);
			chat->jc_sock = -1;
			errno = en;
			return false;
		}
		pos += bytes;
	} while (pos < len);

	return true;
}

static unsigned int
murmur(const void * key, int len)
{
	const unsigned int m = 0x5bd1e995;
	const int r = 24;
	const unsigned int seed = 0x9747b28c;

	unsigned int h = seed ^ len;

	const unsigned char * data = (const unsigned char *)key;

	while (len >= 4) {
		unsigned int k = *(unsigned int *)data;

		k *= m; 
		k ^= k >> r; 
		k *= m; 

		h *= m; 
		h ^= k;

		data += 4;
		len -= 4;
	}

	switch(len)
	{
	case 3: h ^= data[2] << 16;
	case 2: h ^= data[1] << 8;
	case 1: h ^= data[0];
			h *= m;
	};

	h ^= h >> 13;
	h *= m;
	h ^= h >> 15;

	return h;
} 


static const char*
nick_color(const char *nick)
{
	return nick_colors[murmur(nick, strlen(nick)) % (sizeof(nick_colors) / sizeof(*nick_colors))];
}

static void
jtv_chat_handle(struct jtv_chat *chat, char *b, char *e)
{
	unsigned len;
	char *p, *nick, *body;
	if (memcmp(b, "PING ", 5) == 0) {
		static const char reply[] = {'P', 'O', 'N', 'G', '\r', '\n', 0, 0};
		write(chat->jc_sock, reply, sizeof(reply));
		return;
	}

	if (e - b < 4)
		return;
	while (e > b && *(unsigned char*)e < 32)
		*e-- = 0;

	/* parse only chat messages */
//	printf("STANZA: %s\n", b);
	if (*b != ':')
		return;

	for (nick = p = b + 1; *p && *p != '!'; p++) ;

	if (*p != '!')
		return;
	*p++ = 0;

	p = strstr(p, "PRIVMSG #");
	if (!p)
		return;
	p += sizeof("PRIVMSG #") - 1;

	len = strlen(chat->jc_channel);
	if (memcmp(p, chat->jc_channel, len) != 0)
		return;

	p += len;
	if (p[0] != 0x20 && p[1] != ':')
		return;

	body = p + 2;
	if (isascii(*nick))
		*nick = toupper(*nick);
	printf("%s%s%s: %s\n", nick_color(nick), nick, lcc_normal, body);
}

static void*
jtv_chat_thread(void *arg)
{
	struct jtv_chat *chat = arg;
	while (!jtv_chat_connect(chat)) {
		fprintf(stderr, "Can't connect to chat: %s\n", strerror(errno));
		sleep(5);
	}

	while (1) {
		char *b, *e;
		unsigned l;
		ssize_t bytes;

		while (chat->jc_sock == -1 && !jtv_chat_connect(chat)) {
			fprintf(stderr, "Can't connect to chat: %s\n", strerror(errno));
			sleep(5);
		}

		b = chat->jc_buf + chat->jc_buf_len;
		e = chat->jc_buf + sizeof(chat->jc_buf);
		bytes = read(chat->jc_sock, b, e - b);

		/* Handle socket problems */
		if (bytes == -1 || bytes == 0) {
			int e = errno;
			if (e == EINTR || e == EAGAIN)
				continue;
			close(chat->jc_sock);
			chat->jc_buf_len = 0;
			chat->jc_sock = -1;
			fprintf(stderr, "Chat disconnected\n");
			sleep(5);
			continue;
		}

		e = b + bytes;
		b = chat->jc_buf;
		for (l = 0; b + l < e; ) {
			if (b[l] == 0) {
				jtv_chat_handle(chat, b, b + l);
				b += l + 1;
				l = 0;
			} else {
				l++;
			}
		}

		if (b == chat->jc_buf && e == chat->jc_buf + sizeof(chat->jc_buf)) {
			/* just skip long messages */
			chat->jc_buf_len = 0;
		} else if (b != e && b != chat->jc_buf) {
			memmove(chat->jc_buf, b, e - b);
			chat->jc_buf_len = e - b;
		} else {
			chat->jc_buf_len = 0;
		}
	}
	return NULL;
}

static struct jtv_chat *chat;

void
jtv_chat_do(const char *channel)
{
	srand(time(NULL));
	chat = jtv_chat_new( "tmi6.justin.tv", 443, channel);
	if (!chat) {
		fprintf(stderr, "Failed to initialize chat\n");
	} else {
		pthread_create(&chat->jc_thr, NULL, jtv_chat_thread, chat);
	}
	return;
}

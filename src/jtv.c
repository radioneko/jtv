#include "expat/expat.h"
#include <stdio.h>
#include <sys/queue.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include <curl/curl.h>
#include <assert.h>

#include <fcntl.h>
#include <unistd.h>

#include "usher.h"
#include "jtv_memory.h"

static char *read_file_to_string(const char *fname)
{
	int fd = open(fname, O_RDONLY);
	char *res = malloc(65000);
	int b = read(fd, res, 65000 - 1);
	close(fd);
	if (b >= 0) {
		res[b] = 0;
		return res;
	}
	free(res);
	return NULL;
}

struct jtv_state {
	CURL					*http;
	char					*page_url;
	char					*usher_url;
	usher_t					*usher;
	struct jtv_node_list	streams;
	union {
		char				*body;
		char				*swf_url;
	};
	unsigned				body_size;
};

/* id may be twitch or justin.tv URL or user id */
struct jtv_state *
jtv_init(const char *id)
{
	struct jtv_state *j;
	const char *p = strstr(id, ".twitch.tv/"), *e;
	char channel_id[128];
	char buf[256];

	if (p)
		p += sizeof(".twitch.tv/") - 1;
	else if ((p = strstr(id, ".justin.tv/")))
		p += sizeof(".justin.tv/") - 1;
	else {
		p = strrchr(id, '/');
		if (p)
			p++;
		else
			p = id;
	}
	if (!p || !*p) {
		fprintf(stderr, "'%s' is not a valid stream identifier.\n"
				"You must supply either justin/twitch page url or channel name.\n", id);
		exit (EXIT_FAILURE);
	}

	j = xcalloc(1, sizeof(*j));
	j->http = curl_easy_init();

	for (e = p; *e && *e != '/' && *e != '?' && e - p < sizeof(channel_id) - 1; e++)
		/* void */;

	if (e - p == sizeof(channel_id)) {
		fprintf(stderr, "Channel id too long: '%s'\n", p);
		exit (EXIT_FAILURE);
	}

	memcpy(channel_id, p, e - p);
	channel_id[e - p] = 0;

	assert (snprintf(buf, sizeof(buf), "http://www.twitch.tv/%s", channel_id) < sizeof(buf));
	j->page_url = xstrdup(buf);

	assert (snprintf(buf, sizeof(buf), "http://usher.twitch.tv/find/%s.xml?type=any&p=%u&private_code=null&group=",
				channel_id, (unsigned)random()) < sizeof(buf));
	j->usher_url = xstrdup(buf);

	j->usher = usher_new(&j->streams);
	if (!j->usher) {
		fprintf(stderr, "Can't initialize usher parser\n");
		exit (EXIT_FAILURE);
	}

	return j;
}

static size_t
jtv_usher_recv(const void *ptr, size_t size, size_t nmemb, void *userdata)
{
	if (!usher_push_buf((usher_t*)userdata, ptr, size * nmemb)) {
		fprintf(stderr, "usher XML not well formed\n");
		return 0;
	}
	return size * nmemb;
}


/* Fetch usher streams list and ask user to select one */
struct jtv_node*
jtv_select_stream(struct jtv_state *j)
{
	CURLcode code;
	int i;
	long http_code = 0;
	char err[CURL_ERROR_SIZE];
	struct jtv_node *jn;

	curl_easy_setopt(j->http, CURLOPT_URL, j->usher_url);
	curl_easy_setopt(j->http, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(j->http, CURLOPT_WRITEFUNCTION, jtv_usher_recv);
	curl_easy_setopt(j->http, CURLOPT_WRITEDATA, j->usher);
	curl_easy_setopt(j->http, CURLOPT_ERRORBUFFER, err);
	code = curl_easy_perform(j->http);
	if (code != CURLE_OK) {
		fprintf(stderr, "Can't fetch usher status from %s:\n    %s\n", j->usher_url, err);
		exit(EXIT_FAILURE);
	}
	code = curl_easy_getinfo(j->http, CURLINFO_RESPONSE_CODE, &http_code);
	if (code != CURLE_OK) {
		fprintf(stderr, "Can't fetch usher status from %s\n", j->usher_url);
		exit(EXIT_FAILURE);
	}
	if (http_code / 100 != 2) {
		fprintf(stderr, "Can't fetch usher status from %s: HTTP code %ld\n", j->usher_url, http_code);
		exit(EXIT_FAILURE);
	}

	/* Now sort streams */
	jtv_node_list_sort(&j->streams, jtv_node_calculate_priority);

	printf("\nSelect stream to fetch:\n");
	i = 1;
	LIST_FOREACH(jn, &j->streams, jn_link) {
		printf("  %d. \"%s\" (%up)\n", i++, jn->jn_id, jn->jn_vheight);
	}
	while (true) {
		char buf[64];
		int n;
		printf("stream number (default: 1): "); fflush(stdout);
		if (!fgets(buf, sizeof(buf), stdin)) {
			fprintf(stderr, "No stream selected\n");
			return NULL;
		}
		if (buf[0] == 0 || buf[0] == '\r' || buf[0] == '\n') {
			i = 0;
			break;
		}
		if (isdigit(buf[0]) && (n = atoi(buf)) <= i && n > 0) {
			i = n - 1;
			break;
		}
	}

	LIST_FOREACH(jn, &j->streams, jn_link) {
		if (!i)
			return jn;
		i--;
	}

	return NULL;
}

static size_t
jtv_page_recv(const void *ptr, size_t size, size_t nmemb, void *userdata)
{
	struct jtv_state *j = userdata;
	unsigned len = size * nmemb;
	if (len) {
		j->body = xrealloc(j->body, j->body_size + len + 1);
		memcpy(j->body + j->body_size, ptr, len);
		j->body_size += len;
		j->body[j->body_size] = 0;
	}
	return len;
}

void
jtv_fetch_swf_url(struct jtv_state *j)
{
	CURLcode code;
	long http_code = 0;
	char err[CURL_ERROR_SIZE];

	curl_easy_setopt(j->http, CURLOPT_URL, j->page_url);
	curl_easy_setopt(j->http, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(j->http, CURLOPT_WRITEFUNCTION, jtv_page_recv);
	curl_easy_setopt(j->http, CURLOPT_WRITEDATA, j);
	curl_easy_setopt(j->http, CURLOPT_ERRORBUFFER, err);
	code = curl_easy_perform(j->http);
	if (code != CURLE_OK) {
		fprintf(stderr, "Can't fetch justin page from %s:\n    %s\n", j->page_url, err);
		exit(EXIT_FAILURE);
	}
	code = curl_easy_getinfo(j->http, CURLINFO_RESPONSE_CODE, &http_code);
	if (code != CURLE_OK) {
		fprintf(stderr, "Can't fetch justin page from %s\n", j->page_url);
		exit(EXIT_FAILURE);
	}

	if (http_code / 100 != 2) {
		fprintf(stderr, "Can't fetch usher status from %s: HTTP code %ld\n", j->page_url, http_code);
		exit(EXIT_FAILURE);
	}

	if (!j->body) {
		fprintf(stderr, "Empty response from server, url: %s\n", j->page_url);
		exit(EXIT_FAILURE);
	}

	/* Now find swfObject string */
	char *p = j->body;
	static const char s[] = "swfobject.embedSWF(";
	while (1) {
		char *e, c;
		p = strstr(p, s);
		if (!p) {
			fprintf(stderr, "Can't extract swf URL from %s\n", j->page_url);
			exit(EXIT_FAILURE);
		}
		p += sizeof(s) - 1;
		while (isspace(*p)) p++;
		if (*p != '"' && *p != '\'')
			continue;
		p++;
		for (e = p; *e && *e != '"' && *e != '\''; e++)
			/* void */;
		c = *e;
		*e = 0;
		if (strstr(p, "live_site_player") == NULL) {
			*e = c;
			continue;
		}
		e = xstrdup(p);
		free (j->body);
		j->swf_url = e;
		break;
	}
}

static void
print_usage(const char *argv_0, int exit_code)
{
	char *p = strrchr(argv_0, '/');
	p = p ? p + 1 : (char*)argv_0;
	printf("Usage: %s url-or-stream-id\n", p);
	exit(exit_code);
}

int main(int argc, char **argv)
{
	struct jtv_state *jtv;
	struct jtv_node *s;
	const char *rtmpdump = "rtmpdump";

	if (argc < 2)
		print_usage(argv[0], EXIT_FAILURE);

	jtv = jtv_init(argv[1]);
	printf("twitch url: %s\nusher url: %s\n", jtv->page_url, jtv->usher_url);

	jtv_fetch_swf_url(jtv);
	s = jtv_select_stream(jtv);
	curl_easy_cleanup(jtv->http);

	execl(rtmpdump,
			rtmpdump,
			"-r", s->jn_rtmp,
			"-y", s->jn_playpath,
			"-o", "/tmp/1.flv",
			"-s", jtv->swf_url,
			"-f", "LNX 11,2,202,238",
			"-p", jtv->page_url,
			"-j", s->jn_token,
			NULL
			);

	return 0;
}

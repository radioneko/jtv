#include "expat/expat.h"
#include <stdio.h>
#include <sys/queue.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include "usher.h"
#include "jtv_memory.h"

typedef enum {
	JFT_STRING,
	JFT_UINT
} usher_param_t;

struct jtv_usher_param {
	const char		*path;
	usher_param_t	type;
	unsigned		offset;
};

struct jl_xml_path {
	STAILQ_ENTRY(jl_xml_path)	jxp_link;
	unsigned					jxp_len;
	char						jxp_name[0];
};

struct usher_parser {
	XML_Parser						up_xml;
	unsigned						up_level;
	struct jtv_usher_param			*up_param;
	const char						*up_node_id;
	struct jtv_node_list			*up_nodes;
	STAILQ_HEAD(,jl_xml_path)		up_path;
};

struct jtv_node*
jtv_node_new(const char *id)
{
	struct jtv_node *jn = xcalloc(1, sizeof(struct jtv_node));
	jn->jn_id = xstrdup(id);
	return jn;
}

void
jtv_node_free(struct jtv_node *jn)
{
	if (jn) {
		if (jn->jn_playpath)
			free(jn->jn_playpath);
		if (jn->jn_rtmp)
			free(jn->jn_rtmp);
		if (jn->jn_token)
			free(jn->jn_token);
		free(jn);
	}
}

#if 0
#define jn_gen_set_str(id)															\
	char* jtv_node_set_##id(struct jtv_node *jn, const char *pp, unsigned pp_len)	\
	{																				\
		if (jn->jn_##id)															\
			free(jn->jn_##id);														\
		if (pp) {																	\
			if (pp_len) {															\
				jn->jn_##id = xmalloc(pp_len + 1);									\
				memcpy(jn->jn_##id, pp, pp_len);									\
				jn->jn_##id[pp_len] = 0;											\
			} else {																\
				jn->jn_##id = xstrdup(pp);											\
			}																		\
		} else {																	\
			jn->jn_##id = NULL;														\
		}																			\
		return jn->jn_##id;															\
	}

jn_gen_set_str(id)
jn_gen_set_str(playpath)
jn_gen_set_str(rtmp)
jn_gen_set_str(token)
#else
#endif

/* Calculate stream priority based on video resolution and stream name */
int
jtv_node_calculate_priority(struct jtv_node *jn)
{
	int prio = 0;
	if (jn->jn_vheight >= 360)
		prio++;
	if (jn->jn_vheight >= 480)
		prio++;
	if (jn->jn_vheight >= 720)
		prio++;
	if (jn->jn_id && strcmp(jn->jn_id, "720p") == 0)
		prio += 2;
	return prio;
}

/* Just an insertion sort */
void
jtv_node_list_sort(struct jtv_node_list *l, jtv_node_priority_cb prio_cb)
{
	unsigned count = 0, i;
	struct jtv_node *jn = LIST_FIRST(l), *nn;
	while (jn) {
		int prio;
		nn = LIST_NEXT(jn, jn_link);
		prio = prio_cb(jn);
		if (prio < 0) {
			LIST_REMOVE(jn, jn_link);
			jtv_node_free(jn);
		} else {
			jn->jn_priority = prio;
			count++;
		}
		jn = nn;
	}
	while (count) {
		jn = nn = LIST_FIRST(l);
		for (i = count - 1; i; i--) {
			nn = LIST_NEXT(nn, jn_link);
			if (jn->jn_priority >= nn->jn_priority)
				jn = nn;
		}
		if (jn != nn) {
			LIST_REMOVE(jn, jn_link);
			LIST_INSERT_AFTER(nn, jn, jn_link);
		}
		count--;
	}
}

/* lookup node by name. ifnode doesn't exist it will be created */
static struct jtv_node*
jlist_get(struct jtv_node_list *jl, const char *node_id)
{
	struct jtv_node *jn, *last = NULL;
	for (jn = LIST_FIRST(jl); jn; jn = LIST_NEXT(jn, jn_link)) {
		if (strcmp(jn->jn_id, node_id) == 0)
			return jn;
		last = jn;
	}
	jn = jtv_node_new(node_id);
	if (last)
		LIST_INSERT_AFTER(last, jn, jn_link);
	else
		LIST_INSERT_HEAD(jl, jn, jn_link);
	return jn;
}

/* change string field value */
static void
jlist_update_string(struct jtv_node_list *jl, const char *node_id, unsigned offset, const char *val, unsigned val_len)
{
	char **field;
	struct jtv_node *jn = jlist_get(jl, node_id);
	field = (char**)((char*)jn + offset);
	if (*field) {
		unsigned l = strlen(*field);
		*field = xrealloc(*field, l + val_len + 1);
		memcpy(*field + l, val, val_len);
		*(*field + l + val_len) = 0;
	} else {
		*field = xmalloc(val_len + 1);
		memcpy(*field, val, val_len);
		*(*field + val_len) = 0;
	}
}

/* change uint field value */
static void
jlist_update_uint(struct jtv_node_list *jl, const char *node_id, unsigned offset, const char *val, unsigned val_len)
{
	unsigned *field, i;
	struct jtv_node *jn = jlist_get(jl, node_id);
	field = (unsigned*)((char*)jn + offset);
	for (i = 0; i < val_len; i++)
		if (isdigit(val[i]))
			*field = *field * 10 + val[i] - '0';
}

static struct jtv_usher_param usher_data[] = {
	{"/results/@/play",			JFT_STRING,		offsetof(struct jtv_node, jn_playpath)},
	{"/results/@/connect",		JFT_STRING,		offsetof(struct jtv_node, jn_rtmp)},
	{"/results/@/token"	,		JFT_STRING,		offsetof(struct jtv_node, jn_token)},
	{"/results/@/video_height",	JFT_UINT,		offsetof(struct jtv_node, jn_vheight)}
};

/* Perform match check. Return node id.  */
static const char*
jlist_usher_match(struct usher_parser *jxs, struct jtv_usher_param *jud)
{
	struct jl_xml_path *jxp;
	const char *p = jud->path, *path = NULL;
	STAILQ_FOREACH(jxp, &jxs->up_path, jxp_link) {
		if (p[0] == '/' && p[1] == '@' && p[2] == '/') {
			p += 2;
			path = jxp->jxp_name;
		} else if (p[0] == '/') {
			if (memcmp(jxp->jxp_name, p + 1, jxp->jxp_len) != 0 ||
					(p[jxp->jxp_len + 1] != '/' && p[jxp->jxp_len + 1] != 0))
				return NULL;
			p += jxp->jxp_len + 1;
		} else if (p[0] == 0) {
			return path;
		} else {
			fprintf(stderr, "Malformed usher path expression: '%s'\n", jud->path);
			exit(EXIT_FAILURE);
		}
	}
	return *p ? NULL : path;
}

static void
jlist_xml_tag_open(void *data, const char *element, const char **attribute)
{
	unsigned namelen, i;
	struct jl_xml_path *jxp;
	struct usher_parser *jxs = data;
	namelen = strlen(element) + 1;
	jxp = xmalloc(sizeof(*jxp) + namelen);
	jxp->jxp_len = namelen - 1;
	memcpy(jxp->jxp_name, element, namelen);
	STAILQ_INSERT_TAIL(&jxs->up_path, jxp, jxp_link);
	jxs->up_node_id = NULL;
	jxs->up_level++;

	/* Perform pattern match */
	for (i = 0; i < sizeof(usher_data) / sizeof(*usher_data); i++) {
		jxs->up_node_id = jlist_usher_match(jxs, usher_data + i);
		if (jxs->up_node_id) {
			void *addr;
			struct jtv_node *jn = jlist_get(jxs->up_nodes, jxs->up_node_id);
			jxs->up_param = usher_data + i;
			addr = (char*)jn + jxs->up_param->offset;
			switch (jxs->up_param->type) {
			case JFT_STRING:
				*(char**)addr = NULL;
				break;
			case JFT_UINT:
				*(unsigned*)addr = 0;
				break;
			}
			break;
		}
	}
}

static void
jlist_xml_tag_close(void *data, const char *element)
{
	struct usher_parser *jxs = data;
	struct jl_xml_path *jxp = STAILQ_LAST(&jxs->up_path, jl_xml_path, jxp_link);
	if (!jxp) {
		fprintf(stderr, "Closing tag mismatch\n");
		exit(EXIT_FAILURE);
	}
	jxs->up_level--;
	STAILQ_REMOVE(&jxs->up_path, jxp, jl_xml_path, jxp_link);
	free(jxp);
	if (jxs->up_level < 2) {
		jxs->up_param = NULL;
		jxs->up_node_id = NULL;
	}
}

static void
jlist_xml_tag_content(void *data, const char *s, int len)
{
	struct usher_parser *jxs = data;
	if (jxs->up_level == 3 && jxs->up_param && jxs->up_node_id) {
		switch (jxs->up_param->type) {
		case JFT_STRING:
			jlist_update_string(jxs->up_nodes, jxs->up_node_id, jxs->up_param->offset,
					s, len);
			break;
		case JFT_UINT:
			jlist_update_uint(jxs->up_nodes, jxs->up_node_id, jxs->up_param->offset,
					s, len);
			break;
		}
	}
}

usher_t *
usher_new(struct jtv_node_list *streams)
{
	struct usher_parser *up = xcalloc(1, sizeof(*up));
	STAILQ_INIT(&up->up_path);
	LIST_INIT(streams);
	up->up_nodes = streams;
	up->up_xml = XML_ParserCreate("UTF-8");
	if (!up->up_xml) {
		free(up);
		return NULL;
	}
	XML_SetElementHandler(up->up_xml, jlist_xml_tag_open, jlist_xml_tag_close);
	XML_SetCharacterDataHandler(up->up_xml, jlist_xml_tag_content);
	XML_SetUserData(up->up_xml, up);
	return up;
}

void
usher_free(usher_t *u)
{
	struct jl_xml_path *jxp, *next;
	STAILQ_FOREACH_SAFE(jxp, &u->up_path, jxp_link, next) {
		free(jxp);
	}
	XML_ParserFree(u->up_xml);
	free(u);
}

bool
usher_push_buf(usher_t *u, const void *data, unsigned len)
{
	if (XML_Parse(u->up_xml, (const char*)data, len, len ? 0 : 1) != XML_STATUS_OK) {
		fprintf(stderr, "usher ERROR: %s\n", XML_ErrorString(XML_GetErrorCode(u->up_xml)));
		return false;
	}
	return true;
}


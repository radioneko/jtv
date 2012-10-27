#ifndef JTV_USHER_H
#define JTV_USHER_H

#include <sys/queue.h>
#include <stdbool.h>

struct jtv_node {
	char					*jn_id;				/* id of this node */
	char					*jn_playpath;		/* play path (--playpath, -y) */
	char					*jn_rtmp;			/* rtmp url (--rtmp, -r) */
	char					*jn_token;			/* justin.tv token */
	unsigned				jn_vheight;			/* video vertical resolution */
	unsigned				jn_priority;		/* calculated priority of stream */
	LIST_ENTRY(jtv_node)	jn_link;
};

struct jtv_node* jtv_node_new(const char *id);
int jtv_node_calculate_priority(struct jtv_node *jn);
void jtv_node_free(struct jtv_node *jn);


LIST_HEAD(jtv_node_list, jtv_node);
typedef int (*jtv_node_priority_cb)(struct jtv_node *jn);

void jtv_node_list_sort(struct jtv_node_list *jl, jtv_node_priority_cb prio_cb);
void jtv_node_list_free(struct jtv_node_list *jl);

typedef struct usher_parser usher_t;

/* Initialize usher parser context */
usher_t* usher_new(struct jtv_node_list *streams);
/* Free all usher psrsed data */
void usher_free(usher_t *u);
/* Push data to parser */
bool usher_push_buf(usher_t *u, const void *data, unsigned len);


#endif

#ifndef JUSTIN_RTMP_H
#define JUSTIN_RTMP_H

struct jtv_rtmp;

struct jtv_rtmp*
jrtmp_connect(const char *rtmp, const char *playpath,
		const char *page_url, const char *token, const char *swf_url, const char *flashver,
		int outfd);

void jrtmp_run(struct jtv_rtmp *ctx);

#endif

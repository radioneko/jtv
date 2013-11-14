#include "jtv_rtmp.h"
#include "jtv_memory.h"
#include "librtmp/rtmp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

struct jtv_rtmp {
	int			outfd;
	RTMP		rtmp;
	uint32_t	dSeek;
	uint32_t	dStopOffset;
	double		duration;
	int			bResume;
	char		*metaHeader;
	uint32_t	nMetaHeaderSize;
	char		*initialFrame;
	int			initialFrameType;
	uint32_t	nInitialFrameSize;
	int			nSkipKeyFrames;
	int			bStdoutMode;
	int			bLiveStream;
	int			bRealtimeStream;
	int			bOverrideBufferTime;
	uint32_t	bufferTime;
	double		percent;
/*	char		*rtmp;
	char		*playpath;
	char		*swf_url;
	char		*page_url;
	char		*token;
	char		*flashver;*/
};

#define A2AV(av, a) do {				\
		av.av_val = xstrdup(a);			\
		av.av_len = strlen(av.av_val);	\
	} while (0)

struct jtv_rtmp*
jrtmp_connect(const char *rtmp, const char *playpath,
		const char *page_url, const char *token, const char *swf_url, const char *flashver,
		int outfd)
{
	struct jtv_rtmp *r;

	AVal rnull = {0, 0};
	AVal app = {0, 0};
	AVal hostname = {0, 0};
	AVal swfHash = {0, 0};
	AVal pageUrl, flashVer, usherToken, swfUrl, playPath, tcUrl;
	/* Parse RTMP url */
	AVal parsedPlaypath;
	unsigned int port = 0;
	uint32_t swfSize = 0;
	int protocol = RTMP_PROTOCOL_UNDEFINED;
	unsigned char hash[RTMP_SWF_HASHLEN];

	if (!RTMP_ParseURL
			(rtmp, &protocol, &hostname, &port,
			 &parsedPlaypath, &app))
	{
		fprintf(stderr, "Can't parse rtmp url '%s'\n", rtmp);
		exit(EXIT_FAILURE);
	}

	/* AS IS: playpath, page_url, page_url, token, swf_url, flashver */
	A2AV(playPath, playpath);
	A2AV(pageUrl, page_url);
	A2AV(usherToken, token);
	A2AV(swfUrl, swf_url);
	A2AV(flashVer, flashver);
	if (port == 0)
		port = 1935;

	if (strchr(rtmp + sizeof("rtmp://"), ':')) {
		A2AV(tcUrl, rtmp);
	} else {
		char *p = strchr(rtmp + sizeof("rtmp://"), '/');
		if (p) {
			char buf[512];
			snprintf(buf, sizeof(buf), "%.*s:%u%s", (unsigned)(p - rtmp), rtmp, port, p);
			A2AV(tcUrl, buf);
		} else {
			fprintf(stderr, "Can't construct tcUrl\n");
			exit(EXIT_FAILURE);
		}
	}
	
	r = xcalloc(1, sizeof(*r));
	RTMP_Init(&r->rtmp);
	/* Hash SWF */
	if (RTMP_HashSWF(swf_url, &swfSize, hash, 30 /* 30 days swf cache */) == 0) {
		swfHash.av_val = (char*)hash;
		swfHash.av_len = RTMP_SWF_HASHLEN;
	}

	RTMP_SetupStream(&r->rtmp, protocol, &hostname,
			port, &rnull, &playPath, &tcUrl, &swfUrl,
			&pageUrl, &app, &rnull, &swfHash, swfSize,
			&flashVer, &rnull, &usherToken, 0 /*dSeek*/,
			0 /* dStopOffset */, FALSE /* dLiveStream */, 
			30 /* DEF_TIMEOUT */);
	r->rtmp.Link.lFlags |= /*RTMP_LF_BUFX | */RTMP_LF_LIVE;

	RTMP_SetBufferMS(&r->rtmp, 10 * 60 * 60 * 1000);

	if (!RTMP_Connect(&r->rtmp, NULL)) {
		fprintf(stderr, "RTMP_Connect failed\n");
		exit(EXIT_FAILURE);
	}

	if (!RTMP_ConnectStream(&r->rtmp, 0)) {
		fprintf(stderr, "RTMP_ConnectStream failed\n");
		exit(EXIT_FAILURE);
	}

	r->outfd = outfd;

	return r;
}

enum {
	RD_SUCCESS,
	RD_INCOMPLETE,
	RD_FAILED
};

static int
jrtmp_dl(struct jtv_rtmp *r)
{
	RTMP *rtmp = &r->rtmp;
	int bufferSize = 64 * 1024;
	char *buffer;
	int nRead = 0;

	rtmp->m_read.timestamp = r->dSeek;

	r->percent = 0.0;

	if (!r->bLiveStream) {
		// print initial status
		// Workaround to exit with 0 if the file is fully (> 99.9%) downloaded
		if (r->duration > 0) {
			if ((double) rtmp->m_read.timestamp >= (double) r->duration * 999.0) {
				return RD_SUCCESS;
			} else {
				r->percent = ((double) rtmp->m_read.timestamp) / (r->duration * 1000.0) * 100.0;
				r->percent = ((double) (int) (r->percent * 10.0)) / 10.0;
			}
		}
	}

	if (r->bResume && r->nInitialFrameSize > 0)
		rtmp->m_read.flags |= RTMP_READ_RESUME;
	rtmp->m_read.initialFrameType = r->initialFrameType;
	rtmp->m_read.nResumeTS = r->dSeek;
	rtmp->m_read.metaHeader = r->metaHeader;
	rtmp->m_read.initialFrame = r->initialFrame;
	rtmp->m_read.nMetaHeaderSize = r->nMetaHeaderSize;
	rtmp->m_read.nInitialFrameSize = r->nInitialFrameSize;

	buffer = (char *) xmalloc(bufferSize);

	do
	{
		nRead = RTMP_Read(rtmp, buffer, bufferSize);
		//RTMP_LogPrintf("nRead: %d\n", nRead);
		if (nRead > 0)
		{
			if (write(r->outfd, buffer, nRead) !=
					(size_t) nRead)
			{
				fprintf(stderr, "Failed writing, exiting\n");
				free(buffer);
				exit(EXIT_FAILURE);
				//return RD_FAILED;
			}

			//RTMP_LogPrintf("write %dbytes (%.1f kB)\n", nRead, nRead/1024.0);
			if (r->duration <= 0)	// if duration unknown try to get it from the stream (onMetaData)
				r->duration = RTMP_GetDuration(rtmp);

			if (r->duration > 0) {
				// make sure we claim to have enough buffer time!
				if (!r->bOverrideBufferTime && r->bufferTime < (r->duration * 1000.0))
				{
					r->bufferTime = (uint32_t) (r->duration * 1000.0) + 5000;	// extra 5sec to make sure we've got enough

					RTMP_SetBufferMS(rtmp, r->bufferTime);
					RTMP_UpdateBufferMS(rtmp);
				}
				r->percent = ((double) rtmp->m_read.timestamp) / (r->duration * 1000.0) * 100.0;
				r->percent = ((double) (int) (r->percent * 10.0)) / 10.0;
			}
		}
	} while (nRead > -1 && RTMP_IsConnected(rtmp) && !RTMP_IsTimedout(rtmp));
	free(buffer);

	if (nRead < 0)
		nRead = rtmp->m_read.status;

	/* Final status update */
	if (r->bResume && nRead == -2)
	{
		fprintf(stderr, "Couldn't resume FLV file, try --skip %d\n\n",
				r->nSkipKeyFrames + 1);
		return RD_FAILED;
	}

	if (nRead == -3)
		return RD_SUCCESS;

	if ((r->duration > 0 && r->percent < 99.9) || nRead < 0
			|| RTMP_IsTimedout(rtmp))
	{
		return RD_INCOMPLETE;
	}

	return RD_SUCCESS;
}
void jrtmp_run(struct jtv_rtmp *ctx)
{
	jrtmp_dl(ctx);
}

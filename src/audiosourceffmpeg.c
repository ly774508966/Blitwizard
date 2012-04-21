
/* blitwizard 2d engine - source code file

  Copyright (C) 2011-2012 Jonas Thiem

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.

*/

#ifdef USE_AUDIO

#define FFMPEGDEBUG

#ifdef USE_FFMPEG_AUDIO
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "os.h"
#include "logging.h"
#include "audiosource.h"
#include "audiosourceffmpeg.h"
#include "library.h"
#include "filelist.h"
#include "file.h"

#ifndef USE_FFMPEG_AUDIO

// No FFmpeg support!

struct audiosource* audiosourceffmpeg_Create(struct audiosource* source) {
    if (source) {source->close(source);}
    return NULL;
}

void audiosourceffmpeg_DisableFFmpeg() {
    //does nothing in this case, obviously
    return;
}

int audiosourceffmpeg_LoadFFmpeg() {
    return 0; //return failure
}

#else //ifdef USE_FFMPEG_AUDIO

#define AVIOBUFSIZE (AVCODEC_MAX_AUDIO_FRAME_SIZE*2)

#define DECODEBUFUSESIZE (4096*2)
#define DECODEBUFSIZE (DECODEBUFUSESIZE + FF_INPUT_BUFFER_PADDING_SIZE)
#define DECODEBUFMEMMOVETHRESHOLD (4096)
#define DECODEDBUFSIZE 4096

struct audiosourceffmpeg_internaldata {
    struct audiosource* source;
    int sourceeof;
    int eof;
    int returnerroroneof;

    unsigned char* aviobuf;
    unsigned char* buf __attribute__ ((aligned(16)));
    unsigned int bufbytes;
    unsigned int bufoffset;
    char* decodedbuf __attribute__ ((aligned(16)));
    char* tempbuf __attribute__ ((aligned(16)));
    int decodedbufbytes;
    AVCodecContext* codeccontext;
    AVFormatContext* formatcontext;
    AVIOContext* iocontext;
    AVCodec* audiocodec;
    AVPacket packet;
    AVFrame* decodedframe;
};

static int ffmpegopened = 0;
static void* avformatptr;
static void* avcodecptr;
static void* avutilptr;

static void (*ffmpeg_av_register_all)(void);
static AVFormatContext* (*ffmpeg_avformat_alloc_context)(void);
static AVCodecContext* (*ffmpeg_avcodec_alloc_context3)(AVCodec*);
static AVFrame* (*ffmpeg_avcodec_alloc_frame)(void);
static void (*ffmpeg_av_free)(void*);
static void* (*ffmpeg_av_malloc)(size_t);
static AVIOContext* (*ffmpeg_avio_alloc_context)(
    unsigned char* buffer, int buffer_size, int write_flag,
    void* opaque,
    int(*read_packet)(void *opaque, uint8_t *buf, int buf_size),
    int(*write_packet)(void *opaque, uint8_t *buf, int buf_size),
    int64_t(*seek)(void *opaque, int64_t offset, int whence)
);
static int (*ffmpeg_av_find_stream_info)(AVFormatContext*,AVDictionary**);
static int (*ffmpeg_av_find_best_stream)(AVFormatContext*, enum AVMediaType, int, int, AVCodec**, int);
static int (*ffmpeg_avcodec_open2)(AVCodecContext*, AVCodec*, AVDictionary**);
static int (*ffmpeg_avformat_open_input)(AVFormatContext**, const char*, AVInputFormat*, AVDictionary**);
static int (*ffmpeg_avcodec_decode_audio3)(AVCodecContext*, int16_t*, int*, AVPacket*);
static int (*ffmpeg_avcodec_decode_audio4)(AVCodecContext*, AVFrame*, int*, AVPacket*);
static int (*ffmpeg_av_read_frame)(AVFormatContext* s, AVPacket* pkt);
static void (*ffmpeg_av_free_packet)(AVPacket* pkt);
static void (*ffmpeg_av_init_packet)(AVPacket* pkt);
static int (*ffmpeg_av_strerror)(int errnum, char* errbuf, size_t errbuf_size);
static AVCodec* (*ffmpeg_avcodec_find_decoder)(enum CodecID id);
static int (*ffmpeg_av_samples_get_buffer_size)(int*, int, int, enum AVSampleFormat, int);

static int loadorfailstate = 0;
static void loadorfail(void** ptr, void* lib, const char* name) {
    if (loadorfailstate) {return;}
    *ptr = library_GetSymbol(lib, name);
    
    if (!*ptr) {
        printwarning("Warning: [FFmpeg] Failed to load symbol: %s",name);
        loadorfailstate = 1;
        return;
    }
}

static int audiosourceffmpeg_LoadFFmpegFunctions() {
    loadorfail((void**)(&ffmpeg_av_register_all), avformatptr, "av_register_all");
    loadorfail((void**)(&ffmpeg_avformat_alloc_context), avformatptr, "avformat_alloc_context");
    loadorfail((void**)(&ffmpeg_avcodec_alloc_context3), avcodecptr, "avcodec_alloc_context3");
    loadorfail((void**)(&ffmpeg_av_free), avcodecptr, "av_free");
    loadorfail((void**)(&ffmpeg_avio_alloc_context), avformatptr, "avio_alloc_context");
    loadorfail((void**)(&ffmpeg_av_malloc), avformatptr, "av_malloc");
    loadorfail((void**)(&ffmpeg_av_find_stream_info), avformatptr, "av_find_stream_info");
    loadorfail((void**)(&ffmpeg_av_find_best_stream), avformatptr, "av_find_best_stream");
    loadorfail((void**)(&ffmpeg_avcodec_open2), avcodecptr, "avcodec_open2");
    loadorfail((void**)(&ffmpeg_avformat_open_input), avformatptr, "avformat_open_input");
    loadorfail((void**)(&ffmpeg_avcodec_decode_audio3), avcodecptr, "avcodec_decode_audio3");
    loadorfail((void**)(&ffmpeg_av_read_frame), avformatptr, "av_read_frame");
    loadorfail((void**)(&ffmpeg_av_free_packet), avcodecptr, "av_free_packet");
    loadorfail((void**)(&ffmpeg_av_init_packet), avcodecptr, "av_init_packet");
    loadorfail((void**)(&ffmpeg_av_strerror), avutilptr, "av_strerror");
    loadorfail((void**)(&ffmpeg_avcodec_find_decoder), avcodecptr, "avcodec_find_decoder");
    loadorfail((void**)(&ffmpeg_avcodec_alloc_frame), avcodecptr, "avcodec_alloc_frame");

    //decode_audio4 (new, unused variant):
    //loadorfail((void**)(&ffmpeg_avcodec_decode_audio4), avcodecptr, "avcodec_decode_audio4");
    //loadorfail((void**)(&ffmpeg_av_samples_get_buffer_size), avutilptr, "av_samples_get_buffer_size");

    if (loadorfailstate) {
        return 0;
    }
    return 1;
}

static int ffmpegreader(void* data, uint8_t* buf, int buf_size) {
    errno = 0;
    struct audiosource* source = data;
    struct audiosourceffmpeg_internaldata* idata = (struct audiosourceffmpeg_internaldata*)source->internaldata;
    if (idata->sourceeof) {return 0;}
    
    if (idata->source) {
        int i = idata->source->read(idata->source, (void*)buf, (unsigned int)buf_size); 
        errno = 0;
        if (i < 0) {
            idata->returnerroroneof = 1;
        }
        if (i == 0) {
            idata->sourceeof = 1;
        }
        return i;
    }
    return -1;
}

static int audiosourceffmpeg_InitFFmpeg() {
    //Register all available codecs
    ffmpeg_av_register_all();

    return 1;
}

void audiosourceffmpeg_DisableFFmpeg() {
    if (ffmpegopened != 0) {return;}
    ffmpegopened = -1;
    printinfo("[FFmpeg] Use of FFmpeg has been disabled.");
}

int audiosourceffmpeg_LoadFFmpeg() {
    if (ffmpegopened != 0) {
        if (ffmpegopened == 1) {
            return 1;
        }
        return 0;
    }

    //Load libraries
#ifndef WIN
    avutilptr = library_LoadSearch("libavutil");
    avcodecptr = library_LoadSearch("libavcodec");
    avformatptr = library_LoadSearch("libavformat");
#else
    avutilptr = library_LoadSearch("avutil");
    avcodecptr = library_LoadSearch("avcodec");
    avformatptr = library_Loadsearch("avformat");
#endif

    //Check library load state
    if (!avcodecptr || !avformatptr || !avutilptr) {
#ifdef FFMPEGDEBUG
        printinfo("[FFmpeg-debug] A FFmpeg lib pointer is not present");
#endif
        if (avcodecptr) {
            library_Close(avcodecptr);
        }else{
#ifdef FFMPEGDEBUG
            printinfo("[FFmpeg-debug] avcodecptr NOT present");
#endif
        }
        if (avformatptr) {
            library_Close(avformatptr);
        }else{
#ifdef FFMPEGDEBUG
            printinfo("[FFmpeg-debug] avformatptr NOT present");
#endif
        }
        if (avutilptr) {
            library_Close(avutilptr);
        }else{
#ifdef FFMPEGDEBUG
            printinfo("[FFmpeg-debug] avutilptr NOT present");
#endif
        }
    
        printinfo("[FFmpeg] Library not found or cannot be loaded, FFmpeg support will be unavailable");
        ffmpegopened = -1;
        return 0;
    }
    
    //Load functions
    if (!audiosourceffmpeg_LoadFFmpegFunctions()) {
        library_Close(avcodecptr);
        library_Close(avformatptr);
        printinfo("[FFmpeg] Library misses one or more expected symbols. FFmpeg support will be unavailable");
        ffmpegopened = -1;
        return 0;
    }

    //Initialise FFmpeg
    if (!audiosourceffmpeg_InitFFmpeg()) {
        library_Close(avcodecptr);
        library_Close(avformatptr);
        printinfo("[FFmpeg] Library initialisation failed. FFmpeg support will be unavailable");
        ffmpegopened = -1;
        return 0;
    }

    printinfo("[FFmpeg] Library successfully loaded.");
    ffmpegopened = 1;   
    return 1;
}

static void audiosourceffmpeg_Rewind(struct audiosource* source) {
    struct audiosourceffmpeg_internaldata* idata = source->internaldata;
    if (idata->returnerroroneof) {return;}  
    idata->eof = 0;
    if (idata->source) {
        idata->source->rewind(idata->source);
    }
    idata->sourceeof = 0;
}

static void audiosourceffmpeg_FreeFFmpegData(struct audiosource* source) {
    if (!audiosourceffmpeg_LoadFFmpeg()) {
        return;
    }
    struct audiosourceffmpeg_internaldata* idata = source->internaldata;
    if (idata->codeccontext) {
        ffmpeg_av_free(idata->codeccontext);
        idata->codeccontext = NULL;
    }
    if (idata->formatcontext) {
        ffmpeg_av_free(idata->formatcontext);
        idata->formatcontext = NULL;
    }
    if (idata->iocontext) {
        ffmpeg_av_free(idata->iocontext);
        idata->iocontext = NULL;
    }
    if (idata->buf) {
        ffmpeg_av_free(idata->buf);
        idata->buf = NULL;
        idata->bufbytes = 0;
    }
    if (idata->decodedframe) {
        ffmpeg_av_free(idata->decodedframe);
        idata->decodedframe = NULL;
    }
    if (idata->decodedbuf) {
        ffmpeg_av_free(idata->decodedbuf);
        idata->decodedbuf = NULL;
    }
    if (idata->tempbuf) {
        ffmpeg_av_free(idata->tempbuf);
        idata->tempbuf = NULL;
    }
    /*if (idata->aviobuf) { //it appears this is handled by the iocontext
        ffmpeg_av_free(idata->aviobuf);
        idata->aviobuf = NULL;
    }*/
}

static void audiosourceffmpeg_FatalError(struct audiosource* source) {
    audiosourceffmpeg_FreeFFmpegData(source);
    struct audiosourceffmpeg_internaldata* idata = source->internaldata;
    idata->eof = 1;
    idata->returnerroroneof = 1;
}

static int audiosourceffmpeg_Read(struct audiosource* source, char* buffer, unsigned int bytes) {
    struct audiosourceffmpeg_internaldata* idata = source->internaldata;

    if (idata->eof) {
        return -1;
    }
    
    if (!audiosourceffmpeg_LoadFFmpeg()) {
        audiosourceffmpeg_FatalError(source);
        return -1;
    }

    if (!idata->codeccontext) {
        //Get format context
        idata->formatcontext = ffmpeg_avformat_alloc_context();
        if (!idata->formatcontext) {
            audiosourceffmpeg_FatalError(source);
            return -1;
        }

        //av_dict_set(&codec_opts, "request_channels", "2", 0);
        //opts = setup_find_stream_info_opts(ic, codec_opts);

        //Get IO context
        idata->aviobuf = ffmpeg_av_malloc(AVIOBUFSIZE);
        if (!idata->aviobuf) {
            audiosourceffmpeg_FatalError(source);
            return -1;
        }
        idata->iocontext = ffmpeg_avio_alloc_context(idata->aviobuf, AVIOBUFSIZE, 0, source, ffmpegreader, NULL, NULL); 
        if (!idata->iocontext) {
            audiosourceffmpeg_FatalError(source);
            return -1;
        }
        idata->formatcontext->pb = idata->iocontext;
        idata->formatcontext->iformat = NULL;
        
        //Read format
        if (ffmpeg_avformat_open_input(&idata->formatcontext, "", NULL, NULL) != 0) {
            audiosourceffmpeg_FatalError(source);
            return -1;
        }

        //Read detailed stream info
        if (ffmpeg_av_find_stream_info(idata->formatcontext, NULL) < 0) {
            audiosourceffmpeg_FatalError(source);
            return -1;
        }
        
        //Find best stream
        int stream = ffmpeg_av_find_best_stream(idata->formatcontext, AVMEDIA_TYPE_AUDIO, -1, -1, &idata->audiocodec, 0);
        if (stream < 0) {
            audiosourceffmpeg_FatalError(source);
            return -1;
        }

        //Get codec context
        struct AVCodecContext* c = idata->formatcontext->streams[stream]->codec;

        //Find best codec, we don't trust av_find_best_stream on this for now
        idata->audiocodec = ffmpeg_avcodec_find_decoder(c->codec_id);
        if (!idata->audiocodec) {
            //Format is recognised, but codec unsupported
            audiosourceffmpeg_FatalError(source);
            return -1;
        }
    
        //If this isn't an audio stream, we don't want it:
        if (c->codec_type != AVMEDIA_TYPE_AUDIO) {
            audiosourceffmpeg_FatalError(source);
            return -1;
        }

        //Get our actual codec context:
        idata->codeccontext = ffmpeg_avcodec_alloc_context3(idata->audiocodec);

        //Initialise codec. XXX avcodec_open2 is not thread-safe!
        if (ffmpeg_avcodec_open2(idata->codeccontext, idata->audiocodec, NULL) < 0) {
            audiosourceffmpeg_FatalError(source);
            return -1;
        }   

        //Allocate actual decoding buf
        idata->buf = ffmpeg_av_malloc(DECODEBUFSIZE);
        if (!idata->buf) {
            audiosourceffmpeg_FatalError(source);
            return -1;
        }

        idata->decodedbuf = ffmpeg_av_malloc(DECODEDBUFSIZE);
        if (!idata->decodedbuf) {
            audiosourceffmpeg_FatalError(source);
            return -1;
        }
        
        //Remember format data
        source->channels = c->channels;
        source->samplerate = c->sample_rate;

        //Forget about the format context again (we only needed it to probe the format):
        ffmpeg_av_free(idata->formatcontext);
        idata->formatcontext = NULL;

        //Rewind the audio source:
        idata->source->rewind(idata->source); //XXX: DO WE WANT THIS?!

        if (source->channels <= 0 || source->samplerate <= 0) {
#ifdef FFMPEGDEBUG
            printwarning("[FFmpeg-debug] format probing failed: channels or sample rate unknown");
#endif
            //not a known format apparently
            audiosourceffmpeg_FatalError(source);
            return -1;
        }
#ifdef FFMPEGDEBUG
        printinfo("[FFmpeg-debug] audiostream initialised: rate: %d, channels: %d", source->samplerate, source->channels);
#endif
    }

    //we need to return how many bytes we read, so remember it here:
    int writtenbytes = 0;

    //we might have some decoded bytes left:
    if (idata->decodedbufbytes > 0 && bytes > 0) {
        //see how many we want to copy and how many we can actually copy:
        int copybytes = bytes;
        if (copybytes > idata->decodedbufbytes) {
            copybytes = idata->decodedbufbytes;
        }

        //copy bytes to passed buffer:
        memcpy(buffer, idata->decodedbuf, copybytes);
        buffer += copybytes;

        //trim copied bytes from our internal buffer of decoded data:
        if (copybytes < idata->decodedbufbytes) {
            memmove(idata->decodedbuf, idata->decodedbuf + copybytes, DECODEDBUFSIZE - copybytes);
        }
        
        idata->decodedbufbytes -= copybytes;
        bytes -= copybytes;
    }

    while (bytes > 0) {
        //read undecoded data for FFmpeg into the buffer:
        while (idata->bufbytes + idata->bufoffset < DECODEBUFUSESIZE) {
            int i = idata->source->read(idata->source, (char*)idata->buf + idata->bufbytes + idata->bufoffset, DECODEBUFUSESIZE - (idata->bufbytes + idata->bufoffset));
            if (i < 0) { // read error
                audiosourceffmpeg_FatalError(source);
                return -1;
            }
            if (i == 0) {break;} // EOF
            idata->bufbytes += i; // read was successful, continue if required
        }
        if (idata->bufbytes <= 0) {break;} // we haven't read anything new

        //prepare packet
        idata->packet.size = idata->bufbytes;
        idata->packet.data = (unsigned char*)idata->buf + idata->bufoffset;
        idata->buf[DECODEBUFSIZE-1] = 0; //apparently required to avoid MPEG overshooting buffer

        //decode with FFmpeg:
        //decode_audio3 (old, current variant):
        if (!idata->tempbuf) {
            idata->tempbuf = ffmpeg_av_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
        }
        char* outputbuf __attribute__ ((aligned(16))) = idata->tempbuf;
        int bufsize = AVCODEC_MAX_AUDIO_FRAME_SIZE;
        int gotframe = 0;
        int len = ffmpeg_avcodec_decode_audio3(idata->codeccontext, (int16_t*)outputbuf, &bufsize, &idata->packet);
        if (len > 0) {gotframe = 1;}

        //decode_audio4 (new, upcoming variant):
        //int gotframe;
        //int len = ffmpeg_avcodec_decode_audio4(idata->codeccontext, idata->decodedframe, &gotframe, &idata->packet);

        if (len < 0) {
            //A decode error occured:
#ifdef FFMPEGDEBUG
            char errbuf[512] = "Unknown";
            ffmpeg_av_strerror(len, errbuf, sizeof(errbuf)-1);
            errbuf[sizeof(errbuf)-1] = 0;
            printwarning("[FFmpeg-debug] avcodec_decode_audio3 error: %s",errbuf);
#endif
            audiosourceffmpeg_FatalError(source);
            return -1;
        }else{
            idata->bufoffset += len;
            idata->bufbytes -= len;
        }

        //return data if we have some
        if (gotframe) {
            //decode_audio3 (old, current variant):
            int framesize = len;
            const char* p = outputbuf;

            //decode_audio4 (new, upcoming variant):
            //int framesize = ffmpeg_av_samples_get_buffer_size(NULL, idata->codeccontext->channels, idata->decodedframe->nb_samples, idata->codeccontext->sample_fmt, 1);
            //const char* p = (char*)idata->decodedframe->data[0];

            //first, export as much as we can
            if (bytes > 0) {
                //ideally, we would copy the whole frame:
                unsigned int copybytes = framesize;

                //practically, we don't want more than specified in 'bytes':
                if (copybytes > bytes) {copybytes = bytes;}

                //copy the bytes, move the buffers accordingly:
                memcpy(buffer, p, copybytes);
                buffer += copybytes;
                bytes -= copybytes;
                framesize -= copybytes;
                p += copybytes;
                writtenbytes += copybytes;
            }

            //maximal preserval size:
            if (framesize > DECODEDBUFSIZE) {
                //we will simply crop the frame.
                //evil but the show must go on
                framesize = DECODEDBUFSIZE;
            }

            //if we have any bytes left, preserve them now:
            if (framesize > 0) {
                memcpy(idata->decodedbuf, p, framesize);
                idata->decodedbufbytes = framesize;
            }
        }
        //ffmpeg_av_free_packet(idata->packet);

        //check if we want to memmove
        if (idata->bufoffset > DECODEBUFMEMMOVETHRESHOLD) {
            memmove(idata->buf, idata->buf + idata->bufoffset, DECODEBUFSIZE - idata->bufoffset);
            idata->bufoffset = 0;
        }
    }
    printf("ffmpeg: %d bytes read\n",writtenbytes);
    return writtenbytes;
}

static void audiosourceffmpeg_Close(struct audiosource* source) {
    struct audiosourceffmpeg_internaldata* idata = source->internaldata;    
    //close audio source we might have opened
    if (idata && idata->source) {
        idata->source->close(idata->source);
    }
    //close FFmpeg stuff
    audiosourceffmpeg_FreeFFmpegData(source);
    //free all structs & strings
    if (idata) {
        free(idata);
    }
    free(source);
}

#ifdef NOTHREADEDSDLRW
static void audiosourceffmpeg_CloseMainthread(struct audiosource* source) {
    struct audiosourceffmpeg_internaldata* idata = source->internaldata;
    //close audio source we might have opened
    if (idata && idata->source) {
        idata->source->close(idata->source);
        idata->source = NULL;
    }
    audiosourceffmpeg_Close(source);
}
#endif

struct audiosource* audiosourceffmpeg_Create(struct audiosource* source) {
    //without an audio source we can't do anything senseful
    if (!source) {return NULL;}

    //allocate main data struct:
    struct audiosource* a = malloc(sizeof(*a));
    if (!a) {
        if (source) {source->close(source);}
        return NULL;
    }   
    memset(a,0,sizeof(*a));

    //allocate internal data struct:
    a->internaldata = malloc(sizeof(struct audiosourceffmpeg_internaldata));
    if (!a->internaldata) {
        free(a);
        if (source) {source->close(source);}
        return NULL;
    }
    
    //prepare internal data:
    struct audiosourceffmpeg_internaldata* idata = a->internaldata;
    memset(idata, 0, sizeof(*idata));
    idata->source = source;
    if (audiosourceffmpeg_LoadFFmpeg()) {
        ffmpeg_av_init_packet(&idata->packet);
        idata->decodedframe = ffmpeg_avcodec_alloc_frame();
    }

    //without packet data we cannot continue:
    if (!idata->decodedframe) {
        free(idata);
        free(a);
        return NULL;
    }

    a->read = &audiosourceffmpeg_Read;
    a->close = &audiosourceffmpeg_Close;
    a->rewind = &audiosourceffmpeg_Rewind;
#ifdef NOTHREADEDSDLRW
    a->closemainthread = &audiosourceffmpeg_CloseMainthread;
#endif  

    //ensure proper initialisation of sample rate + channels variables
    audiosourceffmpeg_Read(a, NULL, 0);
    if (idata->eof && idata->returnerroroneof) {
        //There was an error reading this ogg file - probably not ogg (or broken ogg)
#ifdef NOTHREADEDSDLRW
        audiosourceffmpeg_CloseMainthread(a);
#else
        audiosourceffmpeg_Close(a);
#endif
        return NULL;
    }
    
    return a;
}

#endif //ifdef USE_FFMPEG_AUDIO

#endif //ifdef USE_AUDIO


/* blitwizard game engine - source code file

  Copyright (C) 2011-2013 Jonas Thiem

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

// #include "os.h"

#ifdef USE_AUDIO

#ifdef USE_FLAC_AUDIO
#include "FLAC/stream_decoder.h"
#endif
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "logging.h"
#include "audiosource.h"
#include "audiosourceflac.h"
#include "library.h"
#include "filelist.h"
#include "file.h"

#ifndef USE_FLAC_AUDIO

//  No FLAC support!

struct audiosource* audiosourceflac_Create(struct audiosource* source) {
    if (source) {
        source->close(source);
    }
    return NULL;
}

#warning "NO FLAC"

#else // ifdef USE_FLAC_AUDIO

// FLAC decoder using libFLAC

#define DECODE_BUFFER (10*1024)

struct audiosourceflac_internaldata {
    // file source (or whereever the undecoded audio comes from):
    struct audiosource* source;
    int filesourceeof;  // "file" audio source has given us an EOF

    // EOF information of this file source:
    int eof;  // we have spilled out an EOF
    int returnerroroneof;  // keep errors in mind

    // buffer for decoded bytes:
    char decodedbuf[DECODE_BUFFER];
    unsigned int decodedbytes;

    // FLAC decoder information:
    FLAC__StreamDecoder* decoder;
    int flacopened;
    int flaceof;  // FLAC decoder has signaled end of stream
};

static void audiosourceflac_Rewind(
struct audiosource* source) {
    struct audiosourceflac_internaldata* idata =
    source->internaldata;
    if (!idata->eof || !idata->returnerroroneof) {
        // free libFLAC decoder data:
        if (idata->flacopened) {
            FLAC__stream_decoder_delete(idata->decoder);
            idata->decoder = NULL;
            idata->flacopened = 0;
        }

        // rewind file source:
        idata->source->rewind(idata->source);
        idata->filesourceeof = 0;
        idata->eof = 0;
        idata->returnerroroneof = 0;

        // reset buffers:
        idata->decodedbytes = 0;
    }
}

static FLAC__bool flaceof(const FLAC__StreamDecoder* decoder,
void* client_data) {
    return ((struct audiosourceflac_internaldata*)
    ((struct audiosource*)client_data)->internaldata)->filesourceeof;
}

static FLAC__StreamDecoderReadStatus flacread(
const FLAC__StreamDecoder* decoder, FLAC__byte buffer[],
size_t* bytes, void* client_data) {
    // read data for the FLAC decoder:
    struct audiosourceflac_internaldata* idata =
    ((struct audiosource*)client_data)->internaldata;

    if (idata->filesourceeof) {
        return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
    }

    int r = idata->source->read(idata->source,
    (char*)buffer, *bytes);
    if (r <= 0) {
        if (r == 0) {
            idata->filesourceeof = 1;
            printf("eof\n");
            return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
        }
        printf("error\n");
        return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    }
    *bytes = r;
    return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
}

FLAC__StreamDecoderWriteStatus flacwrite(
const FLAC__StreamDecoder* decoder,
const FLAC__Frame* frame, const FLAC__int32* const buffer[],
void *client_data) {
    struct audiosource* source = (struct audiosource*)client_data;
    struct audiosourceflac_internaldata* idata =
    source->internaldata;
    
    if (idata->eof && idata->returnerroroneof) {
        // simply ignore data
        return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
    }

    // if we still need format info, grab it now:
    if (source->samplerate == 0) {
        printf("flacwrite info!\n");
        int channels = FLAC__stream_decoder_get_channels(idata->decoder);
        unsigned int bits = FLAC__stream_decoder_get_bits_per_sample(
        idata->decoder);
        unsigned int samplerate = FLAC__stream_decoder_get_sample_rate(
        idata->decoder);
        if (samplerate <= 3000 || channels < 1 || bits < 8 || bits > 32) {
            // this is invalid
            idata->eof = 1;
            idata->returnerroroneof = 1;
            return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
        }
        source->samplerate = samplerate;
        source->channels = channels;
        printf("channels: %u, samplerate: %u\n", channels, samplerate);

        // check if we can deal with the audio format:
        switch (bits) {
        case 8:
            source->format = AUDIOSOURCEFORMAT_U8;
            printf("u8\n");
            break;
        case 16:
            source->format = AUDIOSOURCEFORMAT_S16LE;
            printf("s16le\n");
            break;
        case 24:
            source->format = AUDIOSOURCEFORMAT_S24LE;
            printf("24le\n");
            break;
        case 32:
            source->format = AUDIOSOURCEFORMAT_S32LE;
            printf("s32le\n");
            break;
        default:
            // invalid format
            printf("invalid format\n");
            idata->eof = 1;
            idata->returnerroroneof = 1;
            return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
        }
    }

    // write the data we got:
    unsigned int i = frame->header.blocksize;
    if (i > DECODE_BUFFER - idata->decodedbytes) {
        i = DECODE_BUFFER - idata->decodedbytes;
    }
    memcpy(idata->decodedbuf + idata->decodedbytes, buffer, i);
    idata->decodedbytes += i;
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}

static FLAC__StreamDecoderLengthStatus flaclength(
const FLAC__StreamDecoder* decoder,
FLAC__uint64* stream_length, void* client_data) {
    printf("flaclength\n");fflush(stdout);
    struct audiosource* source = (struct audiosource*)client_data;
    struct audiosourceflac_internaldata* idata =
    source->internaldata;

    size_t length = idata->source->length(idata->source);
    if (length > 0) {
        printf("length returned is: %u\n", length);
        *stream_length = length;
        return FLAC__STREAM_DECODER_LENGTH_STATUS_OK;
    }
    printf("length unsupported\n");
    return FLAC__STREAM_DECODER_LENGTH_STATUS_UNSUPPORTED;
}

static FLAC__StreamDecoderTellStatus flactell(
const FLAC__StreamDecoder* decoder,
FLAC__uint64* absolute_byte_offset, void* client_data) {
    printf("flactell\n");fflush(stdout);
    struct audiosource* source = (struct audiosource*)client_data;
    struct audiosourceflac_internaldata* idata =
    source->internaldata;

    if (idata->source->seekable) {
        size_t pos = idata->source->position(idata->source);
        *absolute_byte_offset = pos;
        return FLAC__STREAM_DECODER_TELL_STATUS_OK;
    }

    // assume it is streamed and position is unsupported:
    return FLAC__STREAM_DECODER_TELL_STATUS_UNSUPPORTED;
}

static FLAC__StreamDecoderSeekStatus flacseek(
const FLAC__StreamDecoder* decoder,
FLAC__uint64 absolute_byte_offset,
void* client_data) {
    printf("flacseek\n");fflush(stdout);
    struct audiosource* source = (struct audiosource*)client_data;
    struct audiosourceflac_internaldata* idata =
    source->internaldata;

    if (idata->source->seekable) {
        // attempt to seek our audio source:
        if (!idata->source->seek(idata->source, absolute_byte_offset)) {
            // seeking failed
            return FLAC__STREAM_DECODER_SEEK_STATUS_ERROR;
        } else {
            // reset our EOF marker if seeking to non-EOF:
            if (idata->filesourceeof &&
            idata->source->position(idata->source) <
            idata->source->length(idata->source)) {
                idata->filesourceeof = 0;
            }
        }
        return FLAC__STREAM_DECODER_SEEK_STATUS_OK;
    }
    return FLAC__STREAM_DECODER_SEEK_STATUS_UNSUPPORTED;
}

static size_t audiosourceflac_Length(struct audiosource* source) {
    struct audiosourceflac_internaldata* idata =
    source->internaldata;

    if (idata->eof && idata->returnerroroneof) {
        return 0;
    }
    if (!idata->flacopened) {
        return 0;
    }
    return FLAC__stream_decoder_get_total_samples(idata->decoder);
}

static int audiosourceflac_Seek(struct audiosource* source, size_t pos) {
    struct audiosourceflac_internaldata* idata =
    source->internaldata;

    if (idata->eof && idata->returnerroroneof) {
        return 0;
    }
    if (!idata->flacopened) {
        return 0;
    }

    // don't allow seeking beyond file end:
    size_t length = audiosourceflac_Length(source);
    if (length == 0) {
        return 0;
    }
    if (pos > length) {
        pos = length;
    }

    if (source->seekable) {
        if (FLAC__stream_decoder_seek_absolute(idata->decoder, pos) == true) {
            idata->eof = 0;
            idata->decodedbytes = 0;
            return 1;
        }
    }
    return 0;
}

static size_t audiosourceflac_Position(struct audiosource* source) {
    struct audiosourceflac_internaldata* idata =
    source->internaldata;

    if (idata->eof && idata->returnerroroneof) {
        return 0;
    }
    if (!idata->flacopened) {
        return 0;
    }

    if (source->seekable) {
        uint64_t pos;
        if (FLAC__stream_decoder_get_decode_position(
        idata->decoder, &pos) == true) {
            return pos;
        }
    }
    return 0;
}

void audiosourceflac_Close(struct audiosource* source) {
    struct audiosourceflac_internaldata* idata =
    source->internaldata;
    if (idata){
        if (idata->flacopened) {
            printf("deleting decoder %p\n", idata->decoder);
            FLAC__stream_decoder_delete(idata->decoder);
            idata->decoder = NULL;
            idata->flacopened = 0;
        }

        if (idata->source) {
            idata->source->close(idata->source);
            idata->source = NULL;
        }
        free(idata);
    }
    free(source);
}

static void flacerror(const FLAC__StreamDecoder* decoder,
FLAC__StreamDecoderErrorStatus status, void* client_data) {
    printf("FLAC ERROR\n");
    // we don't care.
    return;
}

static void flacmetadata(const FLAC__StreamDecoder* decoder,
const FLAC__StreamMetadata* metadata, void* client_data) {
    // we're not interested.
    return;
}

static int audiosourceflac_InitFLAC(struct audiosource* source) {
    struct audiosourceflac_internaldata* idata =
    source->internaldata;

    // open up FLAC decoder:
    idata->decoder = FLAC__stream_decoder_new();
    printf("our decoder is %p\n", idata->decoder);
    if (!idata->decoder) {
        return 0;
    }
    
    idata->flacopened = 1;
    printf("decoder opened\n");fflush(stdout);

    // initialise decoder:
    int r;
    if ((r = FLAC__stream_decoder_init_stream(idata->decoder,
    flacread,
    flacseek,
    flactell,
    flaclength,
    flaceof,
    flacwrite,
    flacmetadata,
    flacerror,
    source
    )) != FLAC__STREAM_DECODER_INIT_STATUS_OK) {
        printf("failed to initialise decoder: %d\n", r);fflush(stdout);
        return 0;
    }
    printf("decoder initialised\n");fflush(stdout);
    if (FLAC__stream_decoder_get_state(idata->decoder) == FLAC__STREAM_DECODER_END_OF_STREAM) {
        printf("stream is EOF\n");
    } else {
        printf("stream is not EOF\n");
    }
    // run decoder up to first audio frame:
    while(idata->decodedbytes == 0) {
        FLAC__bool b;
        if ((b = FLAC__stream_decoder_process_single(idata->decoder)) == false
        || FLAC__stream_decoder_get_state(idata->decoder) ==
        FLAC__STREAM_DECODER_END_OF_STREAM) {
            printf("return value false or end of stream: end of stream: %d, return value: %d\n", FLAC__stream_decoder_get_state(idata->decoder) ==
            FLAC__STREAM_DECODER_END_OF_STREAM, b);
            return 0;
        }
        printf("decoded.\n");
    }
    printf("init ok\n");

    // we should have some information now
    return 1;
}

static int audiosourceflac_Read(
struct audiosource* source, char* buffer,
unsigned int bytes) {
    //printf("audiosourceflac_Read(%u)\n", bytes);
    struct audiosourceflac_internaldata* idata =
    source->internaldata;
    if (idata->eof) {
        idata->returnerroroneof = 1;
        return -1;
    }

    // open up flac file if we don't have one yet
    if (!idata->flacopened) {
        if (!audiosourceflac_InitFLAC(source)) {
            idata->eof = 1;
            idata->returnerroroneof = 1;
            source->samplerate = -1;
            source->channels = -1;
            return -1;
        }
        printf("initialised\n");
    }
    //printf("bytes to read: %d, decodedbytes: %d\n", bytes, idata->decodedbytes);
    int byteswritten = 0;
    while (bytes > 0) {
        if (idata->decodedbytes <= 0) {
            // get more bytes decoded:
            if (idata->filesourceeof) {
                // oops, file end
                break;
            }
            FLAC__bool result = FLAC__stream_decoder_process_single(idata->decoder);
            if (!result || FLAC__stream_decoder_get_state(idata->decoder) ==
            FLAC__STREAM_DECODER_END_OF_STREAM) {
                idata->filesourceeof = 1;
                if (!result) {
                    idata->returnerroroneof = 1;
                }
                // continue since we might still have some decoded bytes left
            }
        }
        unsigned int i = idata->decodedbytes;
        if (i > bytes) {
            i = bytes;
        }
        // write bytes
        memcpy(buffer, idata->decodedbuf, i);
        buffer += i;
        bytes -= i;
        idata->decodedbytes -= i;
        byteswritten += i;
        if (idata->decodedbytes > 0) {
            memmove(idata->decodedbuf, idata->decodedbuf + i, idata->decodedbytes);
        }
    }
    if (bytes > 0 && byteswritten == 0) {
        idata->eof = 1;
        if (idata->returnerroroneof) {
            return -1;
        }
        return 0;
    }
    return byteswritten;
}

struct audiosource* audiosourceflac_Create(struct audiosource* source) {
    // we need a working input source obviously:
    if (!source) {
        return NULL;
    }

    // main data structure
    struct audiosource* a = malloc(sizeof(*a));
    if (!a) {
        source->close(source);
        return NULL;
    }
    memset(a,0,sizeof(*a));

    // internal data structure
    a->internaldata = malloc(sizeof(struct audiosourceflac_internaldata));
    if (!a->internaldata) {
        free(a);
        source->close(source);
        return NULL;
    }
    struct audiosourceflac_internaldata* idata =
    a->internaldata;
    memset(idata, 0, sizeof(*idata));
    idata->source = source;
    printf("source eof: %d\n", idata->filesourceeof);

    // function pointers
    a->read = &audiosourceflac_Read;
    a->close = &audiosourceflac_Close;
    a->rewind = &audiosourceflac_Rewind;
    a->length = &audiosourceflac_Length;
    a->position = &audiosourceflac_Position;
    a->seek = &audiosourceflac_Seek;

    // if our source is seekable, we are so too:
    a->seekable = source->seekable;

    // take an initial peek at the file:
    audiosourceflac_Read(a, NULL, 0);
    if (idata->eof && idata->returnerroroneof) {
        // There was an error reading this ogg file - probably not ogg (or broken ogg)
        audiosourceflac_Close(a);
        return NULL;
    }
    printf("ok!\n");
    return a;
}

#endif // ifdef USE_FLAC_AUDIO

#endif // ifdef USE_AUDIO

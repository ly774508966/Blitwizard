
/* blitwizard 2d engine - source code file

  Copyright (C) 2011 Jonas Thiem

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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "audiosource.h"
#include "audiosourceresample.h"

struct audiosourceresample_internaldata {
	struct audiosource* source;
	int targetrate;
	int eof;

	char unprocessedbuf[512];
	int unprocessedbytes;

	char processedbuf[512];
	int processedbytes;
};

static void audiosourceresample_Close(struct audiosource* source) {
	struct audiosourceresample_internaldata* idata = source->internaldata;
	
	//close the processed source
	if (idata->source) {
		idata->source->close(idata->source);
	}
	
	//free all structs
	if (source->internaldata) {
		free(source->internaldata);
	}
	free(source);
}

static int audiosourceresample_Read(struct audiosource* source, unsigned int bytes, char* buffer) {
	struct audiosourceresample_internaldata* idata = source->internaldata;
	if (idata->eof) {
		return -1;
	}
	
	while (bytes > 0) {
		if (idata->source) {
			//see how many bytes we want to fetch
			int wantsamples = bytes / (sizeof(float) * 2);
			if (wantsamples < sizeof(float) * 2 * bytes) {
				wantsamples += sizeof(float) * 2;
			}

			//fetch new bytes from the source
			while (wantsamples > 0 && idata->unprocessedbytes < sizeof(idata->unprocessedbuf) - sizeof(float) * 2) {
				int i = idata->source->read(idata->source, sizeof(float) * 2, idata->unprocessedbuf + idata->unprocessedbytes);
				wantsamples--;
			}
		}
	}
	return 0;
}

struct audiosource* audiosourceresample_Create(struct audiosource* source, int targetrate) {
	//check if we got a source and if source samplerate + target rate are supported by our limited implementation
	if (!source) {return NULL;}
	if (source->samplerate != 44100 && source->samplerate != 220550 && source->samplerate != 48000) {
		source->close(source);
		return NULL;
	}
	if (targetrate != 48000 && source->samplerate != targetrate) {
		source->close(source);
		return NULL;
	}
	//only allow stereo
	if (source->channels != 2) {
		source->close(source);
		return NULL;
	}

	//allocate data struct
	struct audiosource* a = malloc(sizeof(*a));
	if (!a) {
		source->close(source);
		return NULL;
	}
	
	//allocate data struct for internal (hidden) data
	memset(a,0,sizeof(*a));
	a->internaldata = malloc(sizeof(struct audiosourceresample_internaldata));
	if (a->internaldata) {
		free(a);
		source->close(source);
		return NULL;
	}
	memset(a->internaldata, 0, sizeof(*(a->internaldata)));
	
	//remember some things
	struct audiosourceresample_internaldata* idata = a->internaldata;
	idata->source = source;
	idata->targetrate = targetrate;
	a->samplerate = source->samplerate;
	
	//set function pointers
	a->read = &audiosourceresample_Read;
	a->close = &audiosourceresample_Close;
	
	//complete!
	return a;
}



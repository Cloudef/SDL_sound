/*
 * SDL_sound -- An abstract sound format decoding API.
 * Copyright (C) 2001  Ryan C. Gordon.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Module player for SDL_sound. This driver handles anything that ModPlug does.
 *
 * Please see the file COPYING in the source's root directory.
 *
 *  This file written by Torbj�rn Andersson (d91tan@Update.UU.SE)
 */

#if HAVE_CONFIG_H
#  include <config.h>
#endif

#ifdef SOUND_SUPPORTS_MODPLUG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "SDL_sound.h"

#define __SDL_SOUND_INTERNAL__
#include "SDL_sound_internal.h"

#include "modplug.h"


static int MODPLUG_init(void);
static void MODPLUG_quit(void);
static int MODPLUG_open(Sound_Sample *sample, const char *ext);
static void MODPLUG_close(Sound_Sample *sample);
static Uint32 MODPLUG_read(Sound_Sample *sample);
static int MODPLUG_rewind(Sound_Sample *sample);
static int MODPLUG_seek(Sound_Sample *sample, Uint32 ms);

static const char *extensions_modplug[] =
{
        /* The XMMS plugin is apparently able to load compressed modules as
         * well, but libmodplug does not handle this.
         */
    "669",   /* Composer 669 / UNIS 669 module                              */
    "AMF",   /* ASYLUM Music Format / Advanced Music Format(DSM)            */
    "AMS",   /* AMS module                                                  */
    "DBM",   /* DigiBooster Pro Module                                      */
    "DMF",   /* DMF DELUSION DIGITAL MUSIC FILEFORMAT (X-Tracker)           */
    "DSM",   /* DSIK Internal Format module                                 */
    "FAR",   /* Farandole module                                            */
    "IT",    /* Impulse Tracker IT file                                     */
    "MDL",   /* DigiTracker module                                          */
#if 0
    "J2B",   /* Not implemented? What is it anyway?                         */
#endif
    "MED",   /* OctaMed MED file                                            */
    "MOD",   /* ProTracker / NoiseTracker MOD/NST file                      */
    "MT2",   /* MadTracker 2.0                                              */
    "MTM",   /* MTM file                                                    */
    "OKT",   /* Oktalyzer module                                            */
    "PTM",   /* PTM PolyTracker module                                      */
    "PSM",   /* PSM module                                                  */
    "S3M",   /* ScreamTracker file                                          */
    "STM",   /* ST 2.xx                                                     */
    "ULT",   
    "UMX",
    "XM",    /* FastTracker II                                              */
    NULL
};

const Sound_DecoderFunctions __Sound_DecoderFunctions_MODPLUG =
{
    {
        extensions_modplug,
        "Play modules through ModPlug",
        "Torbj�rn Andersson <d91tan@Update.UU.SE>",
        "http://modplug-xmms.sourceforge.net/"
    },

    MODPLUG_init,       /*   init() method */
    MODPLUG_quit,       /*   quit() method */
    MODPLUG_open,       /*   open() method */
    MODPLUG_close,      /*  close() method */
    MODPLUG_read,       /*   read() method */
    MODPLUG_rewind,     /* rewind() method */
    MODPLUG_seek        /*   seek() method */
};


static int MODPLUG_init(void)
{
    ModPlug_Settings settings;

        /* The settings will require some experimenting. I've borrowed some
         * of them from the XMMS ModPlug plugin.
         */
    settings.mFlags =
          MODPLUG_ENABLE_OVERSAMPLING
        | MODPLUG_ENABLE_NOISE_REDUCTION
        | MODPLUG_ENABLE_REVERB
        | MODPLUG_ENABLE_MEGABASS
        | MODPLUG_ENABLE_SURROUND;
    settings.mChannels = 2;
    settings.mBits = 16;
    settings.mFrequency = 44100;
    settings.mResamplingMode = MODPLUG_RESAMPLE_FIR;
    settings.mReverbDepth = 30;
    settings.mReverbDelay = 100;
    settings.mBassAmount = 40;
    settings.mBassRange = 30;
    settings.mSurroundDepth = 20;
    settings.mSurroundDelay = 20;
    settings.mLoopCount = 0;

    ModPlug_SetSettings(&settings);
    return(1);  /* success. */
} /* MODPLUG_init */


static void MODPLUG_quit(void)
{
    /* it's a no-op. */
} /* MODPLUG_quit */


/*
 * Most MOD files I've seen have tended to be a few hundred KB, even if some
 * of them were much smaller than that.
 */
#define CHUNK_SIZE 65536

static int MODPLUG_open(Sound_Sample *sample, const char *ext)
{
    Sound_SampleInternal *internal = (Sound_SampleInternal *) sample->opaque;
    ModPlugFile *module;
    Uint8 *data;
    size_t size;
    Uint32 retval;
    int has_extension = 0;
    int i;

    /*
     * Apparently ModPlug's loaders are too forgiving. They gladly accept
     *  streams that they shouldn't. For now, rely on file extension instead.
     */
    for (i = 0; extensions_modplug[i] != NULL; i++)
    {
        if (__Sound_strcasecmp(ext, extensions_modplug[i]) == 0)
        {
            has_extension = 1;
            break;
        } /* if */
    } /* for */

    if (!has_extension)
    {
        SNDDBG(("MODPLUG: Unrecognized file type: %s\n", ext));
        Sound_SetError("MODPLUG: Not a module file.");
        return(0);
    } /* if */
    
        /* ModPlug needs the entire stream in one big chunk. I don't like it,
         * but I don't think there's any way around it.
         */
    data = (Uint8 *) malloc(CHUNK_SIZE);
    BAIL_IF_MACRO(data == NULL, ERR_OUT_OF_MEMORY, 0);
    size = 0;

    do
    {
        retval = SDL_RWread(internal->rw, &data[size], 1, CHUNK_SIZE);
        size += retval;
        if (retval == CHUNK_SIZE)
        {
            data = (Uint8 *) realloc(data, size + CHUNK_SIZE);
            BAIL_IF_MACRO(data == NULL, ERR_OUT_OF_MEMORY, 0);
        } /* if */
    } while (retval > 0);

        /* The buffer may be a bit too large, but that doesn't matter. I think
         * it's safe to free it as soon as ModPlug_Load() is finished anyway.
         */
    module = ModPlug_Load((void *) data, size);
    free(data);
    BAIL_IF_MACRO(module == NULL, "MODPLUG: Not a module file.", 0);

    SNDDBG(("MODPLUG: [%d ms] %s\n",
            ModPlug_GetLength(module), ModPlug_GetName(module)));

    sample->actual.channels = 2;
    sample->actual.rate = 44100;
    sample->actual.format = AUDIO_S16SYS;

    internal->decoder_private = (void *) module;
    sample->flags = SOUND_SAMPLEFLAG_NONE;

    SNDDBG(("MODPLUG: Accepting data stream\n"));
    return(1); /* we'll handle this data. */
} /* MODPLUG_open */


static void MODPLUG_close(Sound_Sample *sample)
{
    Sound_SampleInternal *internal = (Sound_SampleInternal *) sample->opaque;
    ModPlugFile *module = (ModPlugFile *) internal->decoder_private;

    ModPlug_Unload(module);
} /* MODPLUG_close */


static Uint32 MODPLUG_read(Sound_Sample *sample)
{
    Sound_SampleInternal *internal = (Sound_SampleInternal *) sample->opaque;
    ModPlugFile *module = (ModPlugFile *) internal->decoder_private;
    int retval;

    retval = ModPlug_Read(module, internal->buffer, internal->buffer_size);
    if (retval == 0)
        sample->flags |= SOUND_SAMPLEFLAG_EOF;
    return(retval);
} /* MODPLUG_read */


static int MODPLUG_rewind(Sound_Sample *sample)
{
    Sound_SampleInternal *internal = (Sound_SampleInternal *) sample->opaque;
    ModPlugFile *module = (ModPlugFile *) internal->decoder_private;

    ModPlug_Seek(module, 0);
    return(1);
} /* MODPLUG_rewind */


static int MODPLUG_seek(Sound_Sample *sample, Uint32 ms)
{
    BAIL_MACRO("!!! FIXME: Not implemented", 0);
} /* MODPLUG_seek */

#endif /* SOUND_SUPPORTS_MODPLUG */


/* end of modplug.c ... */

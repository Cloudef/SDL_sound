/*
   Extended Audio Converter for SDL (Simple DirectMedia Layer)
   Copyright (C) 2002  Frank Ranostaj
                       Institute of Applied Physik
                       Johann Wolfgang Goethe-Universit�t
                       Frankfurt am Main, Germany

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this library; if not, write to the Free
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

   Frank Ranostaj
   ranostaj@stud.uni-frankfurt.de

(This code blatantly abducted for SDL_sound. Thanks, Frank! --ryan.)

*/

#ifndef _INCLUDE_AUDIO_CONVERT_H_
#define _INCLUDE_AUDIO_CONVERT_H_

#include "SDL_audio.h"
#define Sound_AI_Loop 0x2
#define _fsize 64


typedef struct{
   short c[16][2*_fsize];
   char incr[16];
   int pos_mod;
} VarFilter;

typedef struct{
   short* buffer;
   int mode;
   VarFilter *filter;
} AdapterC;

typedef struct{
   int needed;
   VarFilter filter;
   double len_mult;           /* buffer must be len*len_mult big*/
   Uint8* buf;
   int len;
   int    len_cvt;                 /* Length of converted audio buffer */
   int    add;
   int (*adapter[32]) ( AdapterC Data, int length );
} Sound_AudioCVT;

extern DECLSPEC int Sound_ConvertAudio( Sound_AudioCVT *Data );

extern DECLSPEC int Sound_BuildAudioConverter( Sound_AudioCVT *Data,
   Uint16 src_format, Uint8 src_channels, int src_rate,
   Uint16 dst_format, Uint8 dst_channels, int dst_rate );

#endif /* _INCLUDE_AUDIO_CONVERT_H_ */


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
#include "alt_audio_convert.h"
#include <stdlib.h>
#include <math.h>

/* some macros for "parsing" format */

#define IS_8BIT(x)    ((x).format & 0x0008)
#define IS_16BIT(x)   ((x).format & 0x0010)
#define IS_FLOAT(x)   ((x).format & 0x0020) /* !!! FIXME: is this ok? */
#define IS_SIGNED(x)  ((x).format & 0x8000)
#define IS_SYSENDIAN(x) ((AUDIO_U16SYS ^ (x).format) & 0x1000)


/*-------------------------------------------------------------------------*/
/* this filter (Kaiser-window beta=6.8) gives a decent -80dB attentuation  */
static const int filter[_fsize/2] = {
    0, 20798, 0, -6764, 0, 3863, 0, -2560,
    0,  1800, 0, -1295, 0,  936, 0,  -671,
    0,   474, 0,  -326, 0,  217, 0,  -138,
    0,    83, 0,   -46, 0,   23, 0,    -9 };

/* Mono (1 channel ) */
#define Suffix(x) x##1
#include "filter_templates.h"
#undef Suffix

/* Stereo (2 channel ) */
#define Suffix(x) x##2
#include "filter_templates.h"
#undef Suffix

/*-------------------------------------------------------------------------*/
static int ConvertAudio( Sound_AudioCVT *Data,
    Uint8* buffer, int length, int mode )
{
    AdapterC Temp;
    int i;

    /* Make sure there's a converter */
    if( Data == NULL ) {
        SDL_SetError("No converter given");
        return(-1);
    }

    /* Make sure there's data to convert */
    if( buffer == NULL ) {
        SDL_SetError("No buffer allocated for conversion");
        return(-1);
    }

    /* Set up the conversion and go! */
    Temp.buffer = buffer;
    Temp.mode = mode;
    Temp.filter = &Data->filter;

    for( i = 0; Data->adapter[i] != NULL; i++ )
	length = (*Data->adapter[i])( Temp, length);

    return length;
}

int Sound_ConvertAudio( Sound_AudioCVT *Data )
{
    /* !!! FIXME: Try the looping stuff under certain circumstances? --ryan. */
    return ConvertAudio( Data, Data->buf, Data->len, 0 );
}


/*-------------------------------------------------------------------------*/
static int expand8BitTo16BitSys( AdapterC Data, int length )
{
    int i;
    Uint8* inp = Data.buffer;
    Uint16* buffer = (Uint16*)Data.buffer;
    for( i = length; i--; )
         buffer[i] = inp[i]<<8;
    return 2*length;
}

static int expand8BitTo16BitWrong( AdapterC Data, int length )
{
    int i;
    Uint8* inp = Data.buffer;
    Uint16* buffer = (Uint16*)Data.buffer;
    for( i = length; i--; )
         buffer[i] = inp[i];
    return 2*length;
}

/*-------------------------------------------------------------------------*/
static int expand16BitToFloat( AdapterC Data, int length )
{
    int i;
    Sint16* inp = (Sint16*)Data.buffer;
    float* buffer = (float*)Data.buffer;
    for( i = length>>1; i--; )
         buffer[i] = inp[i]*(1./32767);
    return 2*length;
}

/*-------------------------------------------------------------------------*/
static int swapBytes( AdapterC Data, int length )
{
   /*
    * !!! FIXME !!!
    *
    *
    * Use the faster SDL-Macros to swap
    * - Frank
    */

    int i;
    Uint16 a,b;
    Uint16* buffer = (Uint16*) Data.buffer;
    for( i = length>>1; i --; )
    {
         a = b = buffer[i];
         buffer[i] = ( a << 8 ) | ( b >> 8 );
    }
    return length;
}

/*-------------------------------------------------------------------------*/
static int cutFloatTo16Bit( AdapterC Data, int length )
{
    int i;
    float* inp = (float*) Data.buffer;
    Sint16* buffer = (Sint16*) Data.buffer;
    length>>=2;
    for( i = 0; i < length; i++ )
    {
         if( inp[i] > 1. )
             buffer[i] = 32767;
         else if( inp[i] < -1. )
             buffer[i] = -32768;
         else
             buffer[i] = 32767 * inp[i];
    }
    return 2*length;
}

/*-------------------------------------------------------------------------*/
static int cut16BitSysTo8Bit( AdapterC Data, int length )
{
    int i;
    Uint16* inp = (Uint16*) Data.buffer;
    Uint8* buffer = Data.buffer;
    length >>= 1;
    for( i = 0; i < length; i++ )
         buffer[i] = inp[i]>>8;
    return length;
}

static int cut16BitWrongTo8Bit( AdapterC Data, int length )
{
    int i;
    Uint16* inp = (Uint16*) Data.buffer;
    Uint8* buffer = Data.buffer;
    length >>= 1;
    for( i = 0; i < length; i++ )
         buffer[i] = inp[i] & 0xff;
    return length;
}

/*-------------------------------------------------------------------------*/
static int changeSigned( AdapterC Data, int length, int XOR )
{
    int i;
    Uint32* buffer = (Uint32*) Data.buffer;
    for( i = length>>2; i--;  )
         buffer[i] ^= XOR;
    for( i = 4*(length>>2); i < length; i++)
         ((Uint8*)buffer)[i] ^= ((Uint8*)&XOR)[i&3];
    return length;
}

static int changeSigned16BitSys( AdapterC Data, int length )
{
    return changeSigned( Data, length, 0x80008000 );
}

static int changeSigned16BitWrong( AdapterC Data, int length )
{
    return changeSigned( Data, length, 0x00800080 );
}

static int changeSigned8Bit( AdapterC Data, int length )
{
    return changeSigned( Data, length, 0x80808080 );
}

/*-------------------------------------------------------------------------*/
static int convertStereoToMonoS16Bit( AdapterC Data, int length )
{
    int i;
    Sint16* buffer = (Sint16*) Data.buffer;
    Sint16* src = (Sint16*) Data.buffer;
    length >>= 2;
    for( i = 0; i < length;  i++ )
         buffer[i] = ((int) *(src++) + *(src++) ) >> 1;
    return 2*length;
}

static int convertStereoToMonoU16Bit( AdapterC Data, int length )
{
    int i;
    Uint16* buffer = (Uint16*) Data.buffer;
    Uint16* src = (Uint16*) Data.buffer;
    length >>= 2;
    for( i = 0; i < length;  i++ )
         buffer[i] = ((int) *(src++) + *(src++) ) >> 1;
    return 2*length;
}

static int convertStereoToMonoS8Bit( AdapterC Data, int length )
{
    int i;
    Sint8* buffer = (Sint8*) Data.buffer;
    Sint8* src = (Sint8*) Data.buffer;
    length >>= 1;
    for( i = 0; i < length;  i++ )
         buffer[i] = ((int) *(src++) + *(src++) ) >> 1;
    return length;
}

static int convertStereoToMonoU8Bit( AdapterC Data, int length )
{
    int i;
    Uint8* buffer = (Uint8*) Data.buffer;
    Uint8* src = (Uint8*) Data.buffer;
    length >>= 1;
    for( i = 0; i < length;  i++ )
         buffer[i] = ((int) *(src++) + *(src++) ) >> 1;
    return length;
}

/*-------------------------------------------------------------------------*/
static int convertMonoToStereo16Bit( AdapterC Data, int length )
{
    int i;
    Uint16* buffer = (Uint16*) Data.buffer;
    Uint16* dst = (Uint16*)Data.buffer + length;
    for( i = length>>1; i--; )
         *(--dst) = *(--dst) = buffer[i];
    return 2*length;
}

static int convertMonoToStereo8Bit( AdapterC Data, int length )
{
    int i;
    Uint8* buffer = Data.buffer;
    Uint8* dst = Data.buffer + 2*length;
    for( i = length; i--; )
         *(--dst) = *(--dst) = buffer[i];
    return 2*length;
}

/*-------------------------------------------------------------------------*/
static int minus5dB( AdapterC Data, int length )
{
    int i;
    Sint16* buffer = (Sint16*) Data.buffer;
    for(i = length>>1; i--; )
        buffer[i]= 38084 * buffer[i] >> 16;
    return length;
}

/*-------------------------------------------------------------------------*/
static int doubleRateStereo( AdapterC Data, int length )
{
    length >>= 2;
    _doubleRate2( (Sint16*)Data.buffer, Data.mode, length );
    return 4*_doubleRate2( (Sint16*)Data.buffer+1, Data.mode, length );
}

static int doubleRateMono( AdapterC Data, int length )
{
    return 2*_doubleRate1( (Sint16*)Data.buffer, Data.mode, length>>1 );
}

/*-------------------------------------------------------------------------*/
static int halfRateStereo( AdapterC Data, int length )
{
    length >>= 2;
    _halfRate2( (Sint16*)Data.buffer, Data.mode, length );
    return 4*_halfRate2( (Sint16*)Data.buffer+1, Data.mode, length );
}

static int halfRateMono( AdapterC Data, int length )
{
    return 2*_halfRate2( (Sint16*)Data.buffer, Data.mode, length>>1 );
}

/*-------------------------------------------------------------------------*/
static int varRateUpStereo( AdapterC Data, int length )
{
    length >>= 2;
    _varRateUp2( (Sint16*)Data.buffer, Data.mode, Data.filter, length );
    return 4 * _varRateUp2( (Sint16*)Data.buffer+1,
                            Data.mode, Data.filter, length );
}

static int varRateUpMono( AdapterC Data, int length )
{
    return 2 * _varRateUp1( (Sint16*)Data.buffer,
                            Data.mode, Data.filter, length>>1 );
}

static int varRateDownStereo( AdapterC Data, int length )
{
    length >>= 2;
    _varRateDown2( (Sint16*)Data.buffer, Data.mode, Data.filter, length );
    return 2 * _varRateDown2( (Sint16*)Data.buffer+1,
                              Data.mode, Data.filter, length );
}

static int varRateDownMono( AdapterC Data, int length )
{
    return _varRateDown1( (Sint16*)Data.buffer,
                          Data.mode, Data.filter, length>>1 );
}

/*-------------------------------------------------------------------------*/
typedef struct{
    Sint16 denominator;
    Sint16 numerator;
} Fraction;

static Fraction findFraction( float Value )
{
/* gives a maximal error of 3% and typical less than 0.2% */
    const Uint8 frac[96]={
      1, 2,                                                  -1, /*  /1 */
      1,    3,                                               -1, /*  /2 */
         2,    4, 5,                                         -1, /*  /3 */
            3,    5,    7,                                   -1, /*  /4 */
            3, 4,    6, 7, 8, 9,                             -1, /*  /5 */
                  5,    7,           11,                     -1, /*  /6 */
               4, 5, 6,    8, 9, 10, 11, 12, 13,             -1, /*  /7 */
                  5,    7,    9,     11,     13,     15,     -1, /*  /8 */
                  5,    7, 8,    10, 11,     13, 14,     16, -1, /*  /9 */
                        7,    9,     11,     13,             -1, /* /10 */
                     6, 7, 8, 9, 10,     12, 13, 14, 15, 16, -1, /* /11 */
                        7,           11,     13,             -1, /* /12 */
                        7, 8, 9, 10, 11, 12,     14, 15, 16, -1, /* /13 */
                              9,     11,     13,     15,     -1, /* /14 */
                           8,        11,     13, 14,     16, -1, /* /15 */
                              9,     11,     13,     15       }; /* /16 */


    Fraction Result = {0,0};
    int n,num,den=1;

    float RelErr, BestErr = 0;
    if( Value < 31/64. || Value > 64/31. ) return Result;

    for( n = 0; n < sizeof(frac); num=frac[++n] )
    {
         if( num < 0 ) den++;
         RelErr = Value * num / den;
         RelErr = min( RelErr, 1/RelErr );
         if( RelErr > BestErr )
         {
             BestErr = RelErr;
             Result.denominator = den;
             Result.numerator = num;
         }
    }
    return Result;
}

/*-------------------------------------------------------------------------*/
static float sinc( float x )
{
    if( x > -1e-24 && x < 1e-24 ) return 1.;
    else return sin(x)/x;
}

static void calculateVarFilter( Sint16* dst, float Ratio, float phase, float scale )
{
    const Uint16 KaiserWindow7[]= {
        22930, 16292, 14648, 14288, 14470, 14945, 15608, 16404,
        17304, 18289, 19347, 20467, 21644, 22872, 24145, 25460,
        26812, 28198, 29612, 31052, 32513, 33991, 35482, 36983,
        38487, 39993, 41494, 42986, 44466, 45928, 47368, 48782,
        50165, 51513, 52821, 54086, 55302, 56466, 57575, 58624,
        59610, 60529, 61379, 62156, 62858, 63483, 64027, 64490,
        64870, 65165, 65375, 65498, 65535, 65484, 65347, 65124,
        64815, 64422, 63946, 63389, 62753, 62039, 61251, 60391 };
    int i;
    float w;
    const float fg = -.018 + .5 * Ratio;
    const float omega = 2 * M_PI * fg;
    phase -= 63;
    for( i = 0; i < 64; i++)
    {
        w = scale * ( KaiserWindow7[i] * ( i + 1 ));
        dst[i] = w * sinc( omega * (i+phase) );
        dst[127-i] = w * sinc( omega * (127-i+phase) );
    }
}

typedef struct{
    float scale;
    int incr;
} VarFilterMode;

const VarFilterMode Up = { 0.0211952, -1 };
const VarFilterMode Down = { 0.0364733, 1 };

static void setupVarFilter( VarFilter* filter,
                     float Ratio, VarFilterMode Direction )
{
    int i,n,d;
    Fraction IRatio;
    float phase;
    IRatio = findFraction( Ratio );
    Ratio = min( Ratio, 1/Ratio );

    n = IRatio.numerator;
    d = IRatio.denominator;
    filter->pos_mod = d;

    for( i = 0; i < d; i++ )
    {
        if( phase >= n )
        {
            phase -= d;
            filter->incr[i] = Direction.incr;
        }
        else
            filter->incr[i] = 1+Direction.incr;

        calculateVarFilter( filter->c[i], Ratio, phase/(float)n,
                            Direction.scale );
        phase += d;
    }
}

/*-------------------------------------------------------------------------*/
static void createRateConverter( Sound_AudioCVT *Data, int* fip,
                         int SrcRate, int DestRate, int Channel )
{
    int filter_index = *fip;

    int VarPos = 0;
    int Mono = 2 - Channel;
    float Ratio = DestRate;
    *fip = -1;


    if( SrcRate < 1 || SrcRate > 1<<18 ||
        DestRate < 1 || DestRate > 1<<18 ) return;
    Ratio /= SrcRate;

    if( Ratio > 1.)
        VarPos = filter_index++;
    else
        Data->adapter[filter_index++] = minus5dB;

    while( Ratio > 64./31.)
    {
        Data->adapter[filter_index++] =
            Mono ? doubleRateMono : doubleRateStereo;
        Ratio /= 2.;
        Data->len_mult *= 2;
        Data->add *= 2;
        Data->add += _fsize;
    }

    while( Ratio < 31./64. )
    {
        Data->adapter[filter_index++] =
            Mono ? halfRateMono : halfRateStereo;
        Ratio *= 2;
    }

    if( Ratio > 1. )
    {
        setupVarFilter( &Data->filter, Ratio, Up );
        Data->adapter[VarPos] =
            Mono ? varRateUpMono : varRateUpStereo;
        Data->len_mult *= 2;
        Data->add *= 2;
        Data->add += _fsize;
    }
    else
    {
        setupVarFilter( &Data->filter, Ratio, Down );
        Data->adapter[filter_index++] =
            Mono ? varRateDownMono : varRateDownStereo;
    }
    *fip = filter_index;
}

/*-------------------------------------------------------------------------*/
static void createFormatConverter16Bit(Sound_AudioCVT *Data, int* fip,
    SDL_AudioSpec src, SDL_AudioSpec dst )
{
    int filter_index = *fip;

    if( src.channels == 2 && dst.channels == 1 )
    {
        Data->add /= 2;
        Data->len_mult /= 2;

        if( !IS_SYSENDIAN(src) )
            Data->adapter[filter_index++] = swapBytes;

        if( IS_SIGNED(src) )
            Data->adapter[filter_index++] = convertStereoToMonoS16Bit;
        else
            Data->adapter[filter_index++] = convertStereoToMonoU16Bit;

        if( !IS_SYSENDIAN(dst) )
            Data->adapter[filter_index++] = swapBytes;
    }
    else if( IS_SYSENDIAN(src) != IS_SYSENDIAN(dst) )
        Data->adapter[filter_index++] = swapBytes;

    if( IS_SIGNED(src) != IS_SIGNED(dst) )
    {
        if( IS_SYSENDIAN(dst) )
            Data->adapter[filter_index++] = changeSigned16BitSys;
        else
            Data->adapter[filter_index++] = changeSigned16BitSys;
    }

    if( src.channels == 1 && dst.channels == 2 )
    {
        Data->add *= 2;
        Data->len_mult *= 2;
        Data->adapter[filter_index++] = convertMonoToStereo16Bit;
    }

    *fip = filter_index;
}

/*-------------------------------------------------------------------------*/
static void createFormatConverter8Bit(Sound_AudioCVT *Data, int *fip,
    SDL_AudioSpec src, SDL_AudioSpec dst )
{
    int filter_index = *fip;
    if( IS_16BIT(src) )
    {
        Data->add /= 2;
        Data->len_mult /= 2;

        if( IS_SYSENDIAN(src) )
            Data->adapter[filter_index++] = cut16BitSysTo8Bit;
        else
            Data->adapter[filter_index++] = cut16BitWrongTo8Bit;
    }

    if( src.channels == 2 && dst.channels == 1 )
    {
        Data->add /= 2;
        Data->len_mult /= 2;

        if( IS_SIGNED(src) )
            Data->adapter[filter_index++] = convertStereoToMonoS8Bit;
        else
            Data->adapter[filter_index++] = convertStereoToMonoU8Bit;
    }

    if( IS_SIGNED(src) != IS_SIGNED(dst) )
        Data->adapter[filter_index++] = changeSigned8Bit;

    if( src.channels == 1 && dst.channels == 2  )
    {
        Data->add *= 2;
        Data->len_mult *= 2;
        Data->adapter[filter_index++] = convertMonoToStereo8Bit;
    }

    if( !IS_8BIT(dst) )
    {
        Data->add *= 2;
        Data->len_mult *= 2;
        if( IS_SYSENDIAN(dst) )
            Data->adapter[filter_index++] = expand8BitTo16BitSys;
        else
            Data->adapter[filter_index++] = expand8BitTo16BitWrong;
    }

    *fip = filter_index;
}

/*-------------------------------------------------------------------------*/
static void createFormatConverter(Sound_AudioCVT *Data, int *fip,
    SDL_AudioSpec src, SDL_AudioSpec dst )
{
    int filter_index = *fip;

    if( IS_FLOAT(src) )
    {
        Data->adapter[filter_index++] = cutFloatTo16Bit;
        Data->len_mult /= 2;
        Data->add /= 2;
    }

    if( IS_8BIT(src) || IS_8BIT(dst) )
         createFormatConverter8Bit( Data, &filter_index, src, dst);
    else
         createFormatConverter16Bit( Data, &filter_index, src, dst);

    if( IS_FLOAT(dst) )
    {
        Data->adapter[filter_index++] = expand16BitToFloat;
        Data->len_mult *= 2;
        Data->add *= 2;
    }

    *fip = filter_index;
}


/*-------------------------------------------------------------------------*/
DECLSPEC int BuildAudioCVT(Sound_AudioCVT *Data,
    SDL_AudioSpec src, SDL_AudioSpec dst )
{
    SDL_AudioSpec intrm;
    int filter_index = 0;

    if( Data == NULL ) return -1;
    Data->len_mult = 1.;
    Data->add = 0;

    /* Check channels */
    if( src.channels < 1 || src.channels > 2 ||
        dst.channels < 1 || dst.channels > 2 ) goto error_exit;

    /* If no frequency conversion is needed, go straight to dst format */
    if( src.freq == dst.freq )
    {
        createFormatConverter( Data, &filter_index, src, dst );
        goto sucess_exit;
    }

    /* Convert to signed 16Bit System-Endian */
    intrm.format = AUDIO_S16SYS;
    intrm.channels = min( src.channels, dst.channels );
    createFormatConverter( Data, &filter_index, src, intrm );

    /* Do rate conversion */
    if( src.channels == 2 && dst.channels == 2 )
        createRateConverter( Data, &filter_index, src.freq, dst.freq, 2 );
    else
        createRateConverter( Data, &filter_index, src.freq, dst.freq, 1 );
    /* propagate error */
    if( filter_index < 0 ) goto error_exit;

    /* Convert to final format */
    createFormatConverter( Data, &filter_index, intrm, dst );

    /* Set up the filter information */
sucess_exit:
/* !!! FIXME: Is it okay to assign NULL to a function pointer?
              Borland says no. -frank */
    Data->adapter[filter_index] = NULL;
    return 0;

error_exit:
/* !!! FIXME: Is it okay to assign NULL to a function pointer?
              Borland says no. -frank    */
    Data->adapter[0] = NULL;
    return -1;
}

/*-------------------------------------------------------------------------*/
static char *fmt_to_str(Uint16 fmt)
{
    switch (fmt)
    {
        case AUDIO_U8:     return "    U8";
        case AUDIO_S8:     return "    S8";
        case AUDIO_U16MSB: return "U16MSB";
        case AUDIO_S16MSB: return "S16MSB";
        case AUDIO_U16LSB: return "U16LSB";
        case AUDIO_S16LSB: return "S16LSB";
    }
    return "??????";
}

#define AdapterDesc(x) { x, #x }

static void show_AudioCVT( Sound_AudioCVT *Data )
{
    int i,j;
    const struct{ int (*adapter) ( AdapterC, int); Sint8 *name; }
    AdapterDescription[] = {
        AdapterDesc(expand8BitTo16BitSys),
        AdapterDesc(expand8BitTo16BitWrong),
        AdapterDesc(expand16BitToFloat),
        AdapterDesc(swapBytes),
        AdapterDesc(cut16BitSysTo8Bit),
        AdapterDesc(cut16BitWrongTo8Bit),
        AdapterDesc(cutFloatTo16Bit),
        AdapterDesc(changeSigned16BitSys),
        AdapterDesc(changeSigned16BitWrong),
        AdapterDesc(changeSigned8Bit),
        AdapterDesc(convertStereoToMonoS16Bit),
        AdapterDesc(convertStereoToMonoU16Bit),
        AdapterDesc(convertStereoToMonoS8Bit),
        AdapterDesc(convertStereoToMonoU8Bit),
        AdapterDesc(convertMonoToStereo16Bit),
        AdapterDesc(convertMonoToStereo8Bit),
        AdapterDesc(minus5dB),
        AdapterDesc(doubleRateStereo),
        AdapterDesc(doubleRateMono),
        AdapterDesc(halfRateStereo),
        AdapterDesc(halfRateMono),
        AdapterDesc(varRateUpStereo),
        AdapterDesc(varRateUpMono),
        AdapterDesc(varRateDownStereo),
        AdapterDesc(varRateDownMono),
        { NULL,    "----------NULL-----------" }
    };
    const int AdapterDescMax = sizeof(AdapterDescription)
                             / sizeof(*AdapterDescription);

    fprintf( stderr, "\nAdapter List:    \n" );
    for( i = 0; i < 32; i++ )
    {
        for( j = 0; j < AdapterDescMax; j++ )
        {
            if( Data->adapter[i] == AdapterDescription[j].adapter )
            {
                fprintf( stderr, "    %s\n", AdapterDescription[j].name );
                if( Data->adapter[i] == NULL ) return;
                goto cont;
            }
        }
        fprintf( stderr, "    Error: unknown adapter\n" );
        cont:
    }
    fprintf( stderr, "    Error: NULL adapter missing\n" );
}


int Sound_BuildAudioCVT(Sound_AudioCVT *Data,
   Uint16 src_format, Uint8 src_channels, int src_rate,
   Uint16 dst_format, Uint8 dst_channels, int dst_rate)
{
    SDL_AudioSpec src, dst;
    int ret;

    fprintf (stderr,
             "Sound_BuildAudioCVT() :\n"
             "-----------------------\n"
             "format:   %s -> %s\n"
             "channels: %6d -> %6d\n"
             "rate:     %6d -> %6d\n",
             fmt_to_str (src_format), fmt_to_str (dst_format),
             src_channels, dst_channels,
             src_rate, dst_rate);

    src.format = src_format;
    src.channels = src_channels;
    src.freq = src_rate;

    dst.format = dst_format;
    dst.channels = dst_channels;
    dst.freq = dst_rate;

    ret = BuildAudioCVT( Data, src, dst );

    show_AudioCVT( Data );
    fprintf (stderr, "\n"
                     "return value: %d \n", ret );

    return ret;
}

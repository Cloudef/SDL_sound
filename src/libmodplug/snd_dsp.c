/*
 * This source code is public domain.
 *
 * Authors: Olivier Lapicque <olivierl@jps.net>
*/

#include "libmodplug.h"

// Delayed Surround Filters
#define nDolbyHiFltAttn		6
#define nDolbyHiFltMask		3
#define DOLBYATTNROUNDUP	31

// Bass Expansion
#define XBASS_DELAY			14	// 2.5 ms

// Buffer Sizes
#define XBASSBUFFERSIZE		64		// 2 ms at 50KHz
#define FILTERBUFFERSIZE	64		// 1.25 ms
#define SURROUNDBUFFERSIZE	((MAX_SAMPLE_RATE * 50) / 1000)
#define REVERBBUFFERSIZE	((MAX_SAMPLE_RATE * 200) / 1000)
#define REVERBBUFFERSIZE2	((REVERBBUFFERSIZE*13) / 17)
#define REVERBBUFFERSIZE3	((REVERBBUFFERSIZE*7) / 13)
#define REVERBBUFFERSIZE4	((REVERBBUFFERSIZE*7) / 19)


// DSP Effects: PUBLIC members
UINT CSoundFile_m_nXBassDepth = 6;
UINT CSoundFile_m_nXBassRange = XBASS_DELAY;
UINT CSoundFile_m_nReverbDepth = 1;
UINT CSoundFile_m_nReverbDelay = 100;
UINT CSoundFile_m_nProLogicDepth = 12;
UINT CSoundFile_m_nProLogicDelay = 20;

////////////////////////////////////////////////////////////////////
// DSP Effects internal state

// Bass Expansion: low-pass filter
static LONG nXBassSum = 0;
static LONG nXBassBufferPos = 0;
static LONG nXBassDlyPos = 0;
static LONG nXBassMask = 0;

// Noise Reduction: simple low-pass filter
static LONG nLeftNR = 0;
static LONG nRightNR = 0;

// Surround Encoding: 1 delay line + low-pass filter + high-pass filter
static LONG nSurroundSize = 0;
static LONG nSurroundPos = 0;
static LONG nDolbyDepth = 0;
static LONG nDolbyLoDlyPos = 0;
static LONG nDolbyLoFltPos = 0;
static LONG nDolbyLoFltSum = 0;
static LONG nDolbyHiFltPos = 0;
static LONG nDolbyHiFltSum = 0;

// Reverb: 4 delay lines + high-pass filter + low-pass filter
#ifndef MODPLUG_NO_REVERB
static LONG nReverbSize = 0;
static LONG nReverbBufferPos = 0;
static LONG nReverbSize2 = 0;
static LONG nReverbBufferPos2 = 0;
static LONG nReverbSize3 = 0;
static LONG nReverbBufferPos3 = 0;
static LONG nReverbSize4 = 0;
static LONG nReverbBufferPos4 = 0;
static LONG nReverbLoFltSum = 0;
static LONG nReverbLoFltPos = 0;
static LONG nReverbLoDlyPos = 0;
static LONG nFilterAttn = 0;
static LONG gRvbLowPass[8];
static LONG gRvbLPPos = 0;
static LONG gRvbLPSum = 0;
static LONG ReverbLoFilterBuffer[XBASSBUFFERSIZE];
static LONG ReverbLoFilterDelay[XBASSBUFFERSIZE];
static LONG ReverbBuffer[REVERBBUFFERSIZE];
static LONG ReverbBuffer2[REVERBBUFFERSIZE2];
static LONG ReverbBuffer3[REVERBBUFFERSIZE3];
static LONG ReverbBuffer4[REVERBBUFFERSIZE4];
#endif
static LONG XBassBuffer[XBASSBUFFERSIZE];
static LONG XBassDelay[XBASSBUFFERSIZE];
static LONG DolbyLoFilterBuffer[XBASSBUFFERSIZE];
static LONG DolbyLoFilterDelay[XBASSBUFFERSIZE];
static LONG DolbyHiFilterBuffer[FILTERBUFFERSIZE];
static LONG SurroundBuffer[SURROUNDBUFFERSIZE];

// Access the main temporary mix buffer directly: avoids an extra pointer
extern int MixSoundBuffer[MIXBUFFERSIZE*2];
//cextern int MixReverbBuffer[MIXBUFFERSIZE*2];
extern int MixReverbBuffer[MIXBUFFERSIZE*2];

static UINT GetMaskFromSize(UINT len)
//-----------------------------------
{
	UINT n = 2;
	while (n <= len) n <<= 1;
	return ((n >> 1) - 1);
}


void CSoundFile_InitializeDSP(BOOL bReset)
//-----------------------------------------
{
	if (!CSoundFile_m_nReverbDelay) CSoundFile_m_nReverbDelay = 100;
	if (!CSoundFile_m_nXBassRange) CSoundFile_m_nXBassRange = XBASS_DELAY;
	if (!CSoundFile_m_nProLogicDelay) CSoundFile_m_nProLogicDelay = 20;
	if (CSoundFile_m_nXBassDepth > 8) CSoundFile_m_nXBassDepth = 8;
	if (CSoundFile_m_nXBassDepth < 2) CSoundFile_m_nXBassDepth = 2;
	if (bReset)
	{
		// Noise Reduction
		nLeftNR = nRightNR = 0;
	}
	// Pro-Logic Surround
	nSurroundPos = nSurroundSize = 0;
	nDolbyLoFltPos = nDolbyLoFltSum = nDolbyLoDlyPos = 0;
	nDolbyHiFltPos = nDolbyHiFltSum = 0;
	if (CSoundFile_gdwSoundSetup & SNDMIX_SURROUND)
	{
		SDL_memset(DolbyLoFilterBuffer, 0, sizeof(DolbyLoFilterBuffer));
		SDL_memset(DolbyHiFilterBuffer, 0, sizeof(DolbyHiFilterBuffer));
		SDL_memset(DolbyLoFilterDelay, 0, sizeof(DolbyLoFilterDelay));
		SDL_memset(SurroundBuffer, 0, sizeof(SurroundBuffer));
		nSurroundSize = (CSoundFile_gdwMixingFreq * CSoundFile_m_nProLogicDelay) / 1000;
		if (nSurroundSize > SURROUNDBUFFERSIZE) nSurroundSize = SURROUNDBUFFERSIZE;
		if (CSoundFile_m_nProLogicDepth < 8) nDolbyDepth = (32 >> CSoundFile_m_nProLogicDepth) + 32;
		else nDolbyDepth = (CSoundFile_m_nProLogicDepth < 16) ? (8 + (CSoundFile_m_nProLogicDepth - 8) * 7) : 64;
		nDolbyDepth >>= 2;
	}
	// Reverb Setup
#ifndef MODPLUG_NO_REVERB
	if (CSoundFile_gdwSoundSetup & SNDMIX_REVERB)
	{
		UINT nrs = (CSoundFile_gdwMixingFreq * CSoundFile_m_nReverbDelay) / 1000;
		UINT nfa = CSoundFile_m_nReverbDepth+1;
		if (nrs > REVERBBUFFERSIZE) nrs = REVERBBUFFERSIZE;
		if ((bReset) || (nrs != (UINT)nReverbSize) || (nfa != (UINT)nFilterAttn))
		{
			nFilterAttn = nfa;
			nReverbSize = nrs;
			nReverbBufferPos = nReverbBufferPos2 = nReverbBufferPos3 = nReverbBufferPos4 = 0;
			nReverbLoFltSum = nReverbLoFltPos = nReverbLoDlyPos = 0;
			gRvbLPSum = gRvbLPPos = 0;
			nReverbSize2 = (nReverbSize * 13) / 17;
			if (nReverbSize2 > REVERBBUFFERSIZE2) nReverbSize2 = REVERBBUFFERSIZE2;
			nReverbSize3 = (nReverbSize * 7) / 13;
			if (nReverbSize3 > REVERBBUFFERSIZE3) nReverbSize3 = REVERBBUFFERSIZE3;
			nReverbSize4 = (nReverbSize * 7) / 19;
			if (nReverbSize4 > REVERBBUFFERSIZE4) nReverbSize4 = REVERBBUFFERSIZE4;
			SDL_memset(ReverbLoFilterBuffer, 0, sizeof(ReverbLoFilterBuffer));
			SDL_memset(ReverbLoFilterDelay, 0, sizeof(ReverbLoFilterDelay));
			SDL_memset(ReverbBuffer, 0, sizeof(ReverbBuffer));
			SDL_memset(ReverbBuffer2, 0, sizeof(ReverbBuffer2));
			SDL_memset(ReverbBuffer3, 0, sizeof(ReverbBuffer3));
			SDL_memset(ReverbBuffer4, 0, sizeof(ReverbBuffer4));
			SDL_memset(gRvbLowPass, 0, sizeof(gRvbLowPass));
		}
	} else nReverbSize = 0;
#endif
	BOOL bResetBass = FALSE;
	// Bass Expansion Reset
	if (CSoundFile_gdwSoundSetup & SNDMIX_MEGABASS)
	{
		UINT nXBassSamples = (CSoundFile_gdwMixingFreq * CSoundFile_m_nXBassRange) / 10000;
		if (nXBassSamples > XBASSBUFFERSIZE) nXBassSamples = XBASSBUFFERSIZE;
		UINT mask = GetMaskFromSize(nXBassSamples);
		if ((bReset) || (mask != (UINT)nXBassMask))
		{
			nXBassMask = mask;
			bResetBass = TRUE;
		}
	} else
	{
		nXBassMask = 0;
		bResetBass = TRUE;
	}
	if (bResetBass)
	{
		nXBassSum = nXBassBufferPos = nXBassDlyPos = 0;
		SDL_memset(XBassBuffer, 0, sizeof(XBassBuffer));
		SDL_memset(XBassDelay, 0, sizeof(XBassDelay));
	}
}


void CSoundFile_ProcessStereoDSP(int count)
//------------------------------------------
{
#ifndef MODPLUG_NO_REVERB
	// Reverb
	if (CSoundFile_gdwSoundSetup & SNDMIX_REVERB)
	{
		int *pr = MixSoundBuffer, *pin = MixReverbBuffer, rvbcount = count;
		do
		{
			int echo = ReverbBuffer[nReverbBufferPos] + ReverbBuffer2[nReverbBufferPos2]
					+ ReverbBuffer3[nReverbBufferPos3] + ReverbBuffer4[nReverbBufferPos4];	// echo = reverb signal
			// Delay line and remove Low Frequencies			// v = original signal
			int echodly = ReverbLoFilterDelay[nReverbLoDlyPos];	// echodly = delayed signal
			ReverbLoFilterDelay[nReverbLoDlyPos] = echo >> 1;
			nReverbLoDlyPos++;
			nReverbLoDlyPos &= 0x1F;
			int n = nReverbLoFltPos;
			nReverbLoFltSum -= ReverbLoFilterBuffer[n];
			int tmp = echo / 128;
			ReverbLoFilterBuffer[n] = tmp;
			nReverbLoFltSum += tmp;
			echodly -= nReverbLoFltSum;
			nReverbLoFltPos = (n + 1) & 0x3F;
			// Reverb
			int v = (pin[0]+pin[1]) >> nFilterAttn;
			pr[0] += pin[0] + echodly;
			pr[1] += pin[1] + echodly;
			v += echodly >> 2;
			ReverbBuffer3[nReverbBufferPos3] = v;
			ReverbBuffer4[nReverbBufferPos4] = v;
			v += echodly >> 4;
			v >>= 1;
			gRvbLPSum -= gRvbLowPass[gRvbLPPos];
			gRvbLPSum += v;
			gRvbLowPass[gRvbLPPos] = v;
			gRvbLPPos++;
			gRvbLPPos &= 7;
			int vlp = gRvbLPSum >> 2;
			ReverbBuffer[nReverbBufferPos] = vlp;
			ReverbBuffer2[nReverbBufferPos2] = vlp;
			if (++nReverbBufferPos >= nReverbSize) nReverbBufferPos = 0;
			if (++nReverbBufferPos2 >= nReverbSize2) nReverbBufferPos2 = 0;
			if (++nReverbBufferPos3 >= nReverbSize3) nReverbBufferPos3 = 0;
			if (++nReverbBufferPos4 >= nReverbSize4) nReverbBufferPos4 = 0;
			pr += 2;
			pin += 2;
		} while (--rvbcount);
	}
#endif
	// Dolby Pro-Logic Surround
	if (CSoundFile_gdwSoundSetup & SNDMIX_SURROUND)
	{
		int *pr = MixSoundBuffer, n = nDolbyLoFltPos;
		for (int r=count; r; r--)
		{
			int v = (pr[0]+pr[1]+DOLBYATTNROUNDUP) >> (nDolbyHiFltAttn+1);
			v *= (int)nDolbyDepth;
			// Low-Pass Filter
			nDolbyHiFltSum -= DolbyHiFilterBuffer[nDolbyHiFltPos];
			DolbyHiFilterBuffer[nDolbyHiFltPos] = v;
			nDolbyHiFltSum += v;
			v = nDolbyHiFltSum;
			nDolbyHiFltPos++;
			nDolbyHiFltPos &= nDolbyHiFltMask;
			// Surround
			int secho = SurroundBuffer[nSurroundPos];
			SurroundBuffer[nSurroundPos] = v;
			// Delay line and remove low frequencies
			v = DolbyLoFilterDelay[nDolbyLoDlyPos];		// v = delayed signal
			DolbyLoFilterDelay[nDolbyLoDlyPos] = secho;	// secho = signal
			nDolbyLoDlyPos++;
			nDolbyLoDlyPos &= 0x1F;
			nDolbyLoFltSum -= DolbyLoFilterBuffer[n];
			int tmp = secho / 64;
			DolbyLoFilterBuffer[n] = tmp;
			nDolbyLoFltSum += tmp;
			v -= nDolbyLoFltSum;
			n++;
			n &= 0x3F;
			// Add echo
			pr[0] += v;
			pr[1] -= v;
			if (++nSurroundPos >= nSurroundSize) nSurroundPos = 0;
			pr += 2;
		}
		nDolbyLoFltPos = n;
	}
	// Bass Expansion
	if (CSoundFile_gdwSoundSetup & SNDMIX_MEGABASS)
	{
		int *px = MixSoundBuffer;
		int xba = CSoundFile_m_nXBassDepth+1, xbamask = (1 << xba) - 1;
		int n = nXBassBufferPos;
		for (int x=count; x; x--)
		{
			nXBassSum -= XBassBuffer[n];
			int tmp0 = px[0] + px[1];
			int tmp = (tmp0 + ((tmp0 >> 31) & xbamask)) >> xba;
			XBassBuffer[n] = tmp;
			nXBassSum += tmp;
			int v = XBassDelay[nXBassDlyPos];
			XBassDelay[nXBassDlyPos] = px[0];
			px[0] = v + nXBassSum;
			v = XBassDelay[nXBassDlyPos+1];
			XBassDelay[nXBassDlyPos+1] = px[1];
			px[1] = v + nXBassSum;
			nXBassDlyPos = (nXBassDlyPos + 2) & nXBassMask;
			px += 2;
			n++;
			n &= nXBassMask;
		}
		nXBassBufferPos = n;
	}
	// Noise Reduction
	if (CSoundFile_gdwSoundSetup & SNDMIX_NOISEREDUCTION)
	{
		int n1 = nLeftNR, n2 = nRightNR;
		int *pnr = MixSoundBuffer;
		for (int nr=count; nr; nr--)
		{
			int vnr = pnr[0] >> 1;
			pnr[0] = vnr + n1;
			n1 = vnr;
			vnr = pnr[1] >> 1;
			pnr[1] = vnr + n2;
			n2 = vnr;
			pnr += 2;
		}
		nLeftNR = n1;
		nRightNR = n2;
	}
}


/////////////////////////////////////////////////////////////////
// Clean DSP Effects interface

// [Reverb level 0(quiet)-100(loud)], [delay in ms, usually 40-200ms]
BOOL CSoundFile_SetReverbParameters(UINT nDepth, UINT nDelay)
//------------------------------------------------------------
{
	if (nDepth > 100) nDepth = 100;
	UINT gain = nDepth / 20;
	if (gain > 4) gain = 4;
	CSoundFile_m_nReverbDepth = 4 - gain;
	if (nDelay < 40) nDelay = 40;
	if (nDelay > 250) nDelay = 250;
	CSoundFile_m_nReverbDelay = nDelay;
	return TRUE;
}


// [XBass level 0(quiet)-100(loud)], [cutoff in Hz 20-100]
BOOL CSoundFile_SetXBassParameters(UINT nDepth, UINT nRange)
//-----------------------------------------------------------
{
	if (nDepth > 100) nDepth = 100;
	UINT gain = nDepth / 20;
	if (gain > 4) gain = 4;
	CSoundFile_m_nXBassDepth = 8 - gain;	// filter attenuation 1/256 .. 1/16
	UINT range = nRange / 5;
	if (range > 5) range -= 5; else range = 0;
	if (nRange > 16) nRange = 16;
	CSoundFile_m_nXBassRange = 21 - range;	// filter average on 0.5-1.6ms
	return TRUE;
}


// [Surround level 0(quiet)-100(heavy)] [delay in ms, usually 5-50ms]
BOOL CSoundFile_SetSurroundParameters(UINT nDepth, UINT nDelay)
//--------------------------------------------------------------
{
	UINT gain = (nDepth * 16) / 100;
	if (gain > 16) gain = 16;
	if (gain < 1) gain = 1;
	CSoundFile_m_nProLogicDepth = gain;
	if (nDelay < 4) nDelay = 4;
	if (nDelay > 50) nDelay = 50;
	CSoundFile_m_nProLogicDelay = nDelay;
	return TRUE;
}

BOOL CSoundFile_SetWaveConfigEx(BOOL bSurround,BOOL bNoOverSampling,BOOL bReverb,BOOL hqido,BOOL bMegaBass,BOOL bNR,BOOL bEQ)
//----------------------------------------------------------------------------------------------------------------------------
{
	DWORD d = CSoundFile_gdwSoundSetup & ~(SNDMIX_SURROUND | SNDMIX_NORESAMPLING | SNDMIX_REVERB | SNDMIX_HQRESAMPLER | SNDMIX_MEGABASS | SNDMIX_NOISEREDUCTION | SNDMIX_EQ);
	if (bSurround) d |= SNDMIX_SURROUND;
	if (bNoOverSampling) d |= SNDMIX_NORESAMPLING;
	if (bReverb) d |= SNDMIX_REVERB;
	if (hqido) d |= SNDMIX_HQRESAMPLER;
	if (bMegaBass) d |= SNDMIX_MEGABASS;
	if (bNR) d |= SNDMIX_NOISEREDUCTION;
	if (bEQ) d |= SNDMIX_EQ;
	CSoundFile_gdwSoundSetup = d;
	CSoundFile_InitPlayer(FALSE);
	return TRUE;
}
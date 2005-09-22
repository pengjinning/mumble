/* Copyright (C) 2005, Thorvald Natvig <thorvald@natvig.com>

   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright notice,
     this list of conditions and the following disclaimer.
   - Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.
   - Neither the name of the Mumble Developers nor the names of its
     contributors may be used to endorse or promote products derived from this
     software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR
   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <math.h>
#include <QIcon>
#include <QVBoxLayout>

#include "AudioInput.h"
#include "ServerHandler.h"
#include "MainWindow.h"
#include "Player.h"
#include "Plugins.h"
#include "Global.h"

// Remember that we cannot use static member classes that are not pointers, as the constructor
// for AudioInputRegistrar() might be called before they are initialized, as the constructor
// is called from global initialization.
// Hence, we allocate upon first call.

QMap<QString, AudioInputRegistrarNew> *AudioInputRegistrar::qmNew;
QMap<QString, AudioRegistrarConfig> *AudioInputRegistrar::qmConfig;
QString AudioInputRegistrar::current = QString();

AudioInputRegistrar::AudioInputRegistrar(QString name, AudioInputRegistrarNew n, AudioRegistrarConfig c) {
	if (! qmNew)
		qmNew = new QMap<QString, AudioInputRegistrarNew>();
	if (! qmConfig)
		qmConfig = new QMap<QString, AudioRegistrarConfig>();
	qmNew->insert(name,n);
	qmConfig->insert(name,c);
}

AudioInput *AudioInputRegistrar::newFromChoice(QString choice) {
	QSettings qs;
	if (!choice.isEmpty() && qmNew->contains(choice)) {
		qs.setValue("AudioInputDevice", choice);
		current = choice;
		return qmNew->value(current)();
	}
	choice = qs.value("AudioInputDevice").toString();
	if (qmNew->contains(choice)) {
		current = choice;
		return qmNew->value(choice)();
	}

	// Try a sensible default. For example, ASIO is NOT a sensible default, but it's
	// pretty early in the sorted map.

	if (qmNew->contains("DirectSound")) {
		current = "DirectSound";
		return qmNew->value(current)();
	}

	QMapIterator<QString, AudioInputRegistrarNew> i(*qmNew);
	if (i.hasNext()) {
		i.next();
		current = i.key();
		return i.value()();
	}
	return NULL;
}

int AudioInput::c_iFrameCounter = 0;

AudioInput::AudioInput()
{
	speex_bits_init(&sbBits);
	esEncState=speex_encoder_init(&speex_wb_mode);
	speex_encoder_ctl(esEncState,SPEEX_GET_FRAME_SIZE,&iFrameSize);

	iByteSize=iFrameSize * 2;

	int iarg=1;
	speex_encoder_ctl(esEncState,SPEEX_SET_VBR, &iarg);

	iarg = 0;

	speex_encoder_ctl(esEncState,SPEEX_SET_VAD, &iarg);
	speex_encoder_ctl(esEncState,SPEEX_SET_DTX, &iarg);

	bResetProcessor = true;

	sppPreprocess = NULL;
	sesEcho = NULL;

	psMic = new short[iFrameSize];
	psSpeaker = new short[iFrameSize];
	psClean = new short[iFrameSize];
	pfY = new float[iFrameSize+1];

	bHasSpeaker = false;

	iBitrate = 0;
	dSnr = dLoudness = dPeakMic = dPeakSpeaker = dSpeechProb = 0.0;

	bRunning = false;
}

AudioInput::~AudioInput()
{
	bRunning = false;
	wait();
	speex_bits_destroy(&sbBits);
	speex_encoder_destroy(esEncState);

	if (sppPreprocess)
		speex_preprocess_state_destroy(sppPreprocess);
	if (sesEcho)
		speex_echo_state_destroy(sesEcho);

	delete [] psMic;
	delete [] psSpeaker;
	delete [] psClean;
	delete [] pfY;
}

void AudioInput::encodeAudioFrame() {
	int iArg;
	float fArg;
	int iLen;
	Player *p=Player::get(g.sId);
	short max;
	int i;

	short *psSource;

	c_iFrameCounter++;

	if (! bRunning) {
		return;
	}

	max=1;
	for(i=0;i<iFrameSize;i++)
		if (abs(psMic[i]) > max)
			max=abs(psMic[i]);
	dPeakMic=20.0*log10((max  * 1.0L) / 32768.0L);

	if (bHasSpeaker) {
		max=1;
		for(i=0;i<iFrameSize;i++)
			if (abs(psSpeaker[i]) > max)
				max=abs(psSpeaker[i]);
		dPeakSpeaker=20.0*log10((max  * 1.0L) / 32768.0L);
	} else {
		dPeakSpeaker = 0.0;
	}

	if (bResetProcessor) {
		if (sppPreprocess)
			speex_preprocess_state_destroy(sppPreprocess);

		sppPreprocess = speex_preprocess_state_init(iFrameSize, SAMPLE_RATE);

		if (bHasSpeaker) {
			if (sesEcho)
				speex_echo_state_destroy(sesEcho);
			sesEcho = speex_echo_state_init(iFrameSize, iFrameSize*10);
			qWarning("AudioInput: ECHO CANCELLER ACTIVE");
		}

		bResetProcessor = false;
	}

	fArg = g.s.iQuality;
	speex_encoder_ctl(esEncState,SPEEX_SET_VBR_QUALITY, &fArg);

	iArg = g.s.iComplexity;
	speex_encoder_ctl(esEncState,SPEEX_SET_COMPLEXITY, &iArg);

	iArg = 1;
	speex_preprocess_ctl(sppPreprocess, SPEEX_PREPROCESS_SET_VAD, &iArg);
	speex_preprocess_ctl(sppPreprocess, SPEEX_PREPROCESS_SET_DENOISE, &iArg);
	speex_preprocess_ctl(sppPreprocess, SPEEX_PREPROCESS_SET_AGC, &iArg);
	speex_preprocess_ctl(sppPreprocess, SPEEX_PREPROCESS_SET_DEREVERB, &iArg);

	fArg = 20000;
	speex_preprocess_ctl(sppPreprocess, SPEEX_PREPROCESS_SET_AGC_LEVEL, &fArg);

	int iIsSpeech;

	if (bHasSpeaker) {
		speex_echo_cancel(sesEcho, psMic, psSpeaker, psClean, pfY);
		iIsSpeech=speex_preprocess(sppPreprocess, psClean, pfY);
		psSource = psClean;
	} else {
		iIsSpeech=speex_preprocess(sppPreprocess, psMic, NULL);
		psSource = psMic;
	}

	max=1;
	for(i=0;i<iFrameSize;i++)
		if (abs(psSource[i]) > max)
			max=abs(psSource[i]);
	dPeakSignal=20.0*log10((max  * 1.0L) / 32768.0L);

	// The default is a bit short, increase it
	if (! iIsSpeech && sppPreprocess->last_speech < g.s.iVoiceHold)
		iIsSpeech = 1;

	if (sppPreprocess->loudness2 < g.s.iMinLoudness)
		sppPreprocess->loudness2 = g.s.iMinLoudness;

	if (g.s.atTransmit == Settings::PushToTalk)
		iIsSpeech = g.bPushToTalk || g.bCenterPosition;
	else if (g.s.atTransmit == Settings::Continous)
		iIsSpeech = 1;

	if (g.s.bMute || (p && p->bMute)) {
		iIsSpeech = 0;
	}

	dSnr = sppPreprocess->Zlast;
	dLoudness = sppPreprocess->loudness2;
	dSpeechProb = sppPreprocess->speech_prob;

	if (p)
		p->setTalking(iIsSpeech);

	if (! iIsSpeech && ! bPreviousVoice) {
		iBitrate = 0;
		return;
	}

	bPreviousVoice = iIsSpeech;

	if (! iIsSpeech) {
		memset(psMic, 0, iByteSize);
	}

	speex_bits_reset(&sbBits);
	speex_encode_int(esEncState, psSource, &sbBits);
	speex_encoder_ctl(esEncState, SPEEX_GET_BITRATE, &iBitrate);
	speex_bits_pack(&sbBits, (iIsSpeech) ? 1 : 0, 1);

	if ((g.s.ptTransmit != Settings::Nothing) && g.p && ! g.bCenterPosition) {
		g.p->fetch();
		if (g.p->bValidPos) {
			QByteArray q;
			QDataStream ds(&q, QIODevice::WriteOnly);
			ds << g.p->fPosition[0];
			ds << g.p->fPosition[1];
			ds << g.p->fPosition[2];
			if (g.p->bValidVel && (g.s.ptTransmit == Settings::PositionVelocity)) {
				ds << g.p->fVelocity[0];
				ds << g.p->fVelocity[1];
				ds << g.p->fVelocity[2];
			}
			const unsigned char *d=reinterpret_cast<const unsigned char*>(q.data());
			for(i=0;i<q.size();i++) {
				speex_bits_pack(&sbBits, d[i], 8);
			}
		}
	}

	iLen=speex_bits_nbytes(&sbBits);
	QByteArray qbaPacket(iLen, 0);
	speex_bits_write(&sbBits, qbaPacket.data(), iLen);

	MessageSpeex msPacket;
	msPacket.qbaSpeexPacket = qbaPacket;
	msPacket.iSeq = c_iFrameCounter;
	if (g.sh)
		g.sh->sendMessage(&msPacket);
}

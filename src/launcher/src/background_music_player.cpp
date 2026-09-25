// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from shadPS4 Shadlix fork's background_music_player.cpp
// Adapted: uses forward-declared QMediaPlayer/QAudioOutput (no hard Qt6 Multimedia dep)

#include "background_music_player.h"

#include <QUrl>

// When Qt6 Multimedia is not linked, these are just null pointers.
// The methods become no-ops. When it IS linked, uncomment the
// #define USE_QT_MULTIMEDIA below and link Qt6::Multimedia.
// #define USE_QT_MULTIMEDIA

BackgroundMusicPlayer::BackgroundMusicPlayer(QObject* parent) : QObject(parent) {
#ifdef USE_QT_MULTIMEDIA
	m_mediaPlayer = new QMediaPlayer(this);
	m_audioOutput = new QAudioOutput(this);
	m_mediaPlayer->setAudioOutput(m_audioOutput);
	m_audioOutput->setVolume(static_cast<float>(m_volume) / 100.0f);

	connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged, [this](QMediaPlayer::MediaStatus status) {
		if (status == QMediaPlayer::EndOfMedia && m_playing) {
			m_mediaPlayer->setPosition(0);
			m_mediaPlayer->play();
		}
	});
#endif
}

void BackgroundMusicPlayer::setVolume(int volume) {
	m_volume = volume;
#ifdef USE_QT_MULTIMEDIA
	if (m_audioOutput) {
		m_audioOutput->setVolume(static_cast<float>(volume) / 100.0f);
	}
#endif
}

void BackgroundMusicPlayer::playMusic(const QString& snd0path, bool loops) {
	if (snd0path.isEmpty()) return;
#ifdef USE_QT_MULTIMEDIA
	if (!m_mediaPlayer) return;
	m_mediaPlayer->setSource(QUrl::fromLocalFile(snd0path));
	m_mediaPlayer->play();
	m_playing = true;
#else
	(void)loops; // No-op without Qt6 Multimedia
#endif
}

void BackgroundMusicPlayer::stopMusic() {
#ifdef USE_QT_MULTIMEDIA
	if (m_mediaPlayer) m_mediaPlayer->stop();
#endif
	m_playing = false;
}

void BackgroundMusicPlayer::pauseMusic() {
#ifdef USE_QT_MULTIMEDIA
	if (m_mediaPlayer) m_mediaPlayer->pause();
#endif
	m_playing = false;
}

void BackgroundMusicPlayer::resumeMusic() {
#ifdef USE_QT_MULTIMEDIA
	if (m_mediaPlayer) m_mediaPlayer->play();
#endif
	m_playing = true;
}

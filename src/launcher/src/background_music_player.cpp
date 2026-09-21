// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from shadPS4 Shadlix fork's background_music_player.cpp

#include "background_music_player.h"

#include <QUrl>

BackgroundMusicPlayer::BackgroundMusicPlayer(QObject* parent) : QObject(parent) {
	m_mediaPlayer = new QMediaPlayer(this);
	m_audioOutput = new QAudioOutput(this);
	m_mediaPlayer->setAudioOutput(m_audioOutput);
	m_audioOutput->setVolume(static_cast<float>(m_volume) / 100.0f);

	connect(m_mediaPlayer, &QMediaPlayer::mediaStatusChanged, [this](QMediaPlayer::MediaStatus status) {
		if (status == QMediaPlayer::EndOfMedia && m_playing) {
			// Loop the music
			m_mediaPlayer->setPosition(0);
			m_mediaPlayer->play();
		}
	});
}

void BackgroundMusicPlayer::setVolume(int volume) {
	m_volume = volume;
	m_audioOutput->setVolume(static_cast<float>(volume) / 100.0f);
}

void BackgroundMusicPlayer::playMusic(const QString& snd0path, bool loops) {
	if (snd0path.isEmpty()) {
		return;
	}

	m_currentMusic = QUrl::fromLocalFile(snd0path);
	m_mediaPlayer->setSource(m_currentMusic);
	m_mediaPlayer->play();
	m_playing = true;

	// Note: looping is handled in the mediaStatusChanged callback above
	(void)loops; // Always loop for now — can be made configurable later
}

void BackgroundMusicPlayer::stopMusic() {
	m_mediaPlayer->stop();
	m_playing = false;
}

void BackgroundMusicPlayer::pauseMusic() {
	m_mediaPlayer->pause();
	m_playing = false;
}

void BackgroundMusicPlayer::resumeMusic() {
	m_mediaPlayer->play();
	m_playing = true;
}

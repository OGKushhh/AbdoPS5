// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from shadPS4 Shadlix fork's background_music_player.h

#pragma once

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QObject>
#include <QString>

// Background music player for the launcher UI.
// Plays ambient background music while browsing the game list.
// Ported from the shadPS4 Shadlix fork, adapted for Qt6/KytyPS5.

class BackgroundMusicPlayer : public QObject {
	Q_OBJECT

public:
	static BackgroundMusicPlayer& getInstance() {
		static BackgroundMusicPlayer instance;
		return instance;
	}

	void setVolume(int volume);
	void playMusic(const QString& snd0path, bool loops = true);
	void stopMusic();
	void pauseMusic();
	void resumeMusic();

	[[nodiscard]] bool isPlaying() const { return m_playing; }
	[[nodiscard]] int getVolume() const { return m_volume; }

private:
	BackgroundMusicPlayer(QObject* parent = nullptr);

	QMediaPlayer* m_mediaPlayer;
	QAudioOutput* m_audioOutput;
	QUrl         m_currentMusic;
	bool         m_playing = false;
	int          m_volume   = 50;
};

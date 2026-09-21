// SPDX-FileCopyrightText: Copyright 2026 AbdoPS5 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Ported from shadPS4 Shadlix fork's background_music_player.h

#pragma once

#include <QObject>
#include <QString>

// Forward declare Qt Multimedia types to avoid hard dependency.
// When Qt6 Multimedia is available, link against it and the real types work.
// When not available, BackgroundMusicPlayer is a no-op (methods return without doing anything).
class QMediaPlayer;
class QAudioOutput;

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

	QMediaPlayer* m_mediaPlayer = nullptr;
	QAudioOutput* m_audioOutput = nullptr;
	bool          m_playing = false;
	int           m_volume   = 50;
};

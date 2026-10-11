#pragma once

#include <QElapsedTimer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QWidget>

struct obs_source;

class QCheckBox;
class QDragEnterEvent;
class QDropEvent;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QTableWidget;

struct Track {
	QString path;
	QString title;
	QString artist;
	QString album;
	qint64 durationMs = 0;
};

class MusicDock : public QWidget {
	Q_OBJECT

public:
	explicit MusicDock(const QString &configFile, QWidget *parent = nullptr);
	~MusicDock() override;

	// Detiene la música y libera la fuente de OBS (se llama al cerrar OBS)
	void shutdown();

protected:
	void dragEnterEvent(QDragEnterEvent *e) override;
	void dropEvent(QDropEvent *e) override;

private slots:
	void removeSelected();
	void clearAll();

	void onPlayPause();
	void onStop();
	void onNext();
	void onPrev();
	void onRowDoubleClicked(int row, int column);

	void onTick();
	void onSeekReleased();
	void onVolumeChanged(int value);
	void onMonitorToggled(bool checked);

private:
	void buildUi();
	void ensureSource();
	void playIndex(int row);
	void advance(bool automatic);
	void stopPlayback();
	void applyMonitoring();

	void loadFiles(bool replace);
	void addPaths(const QStringList &paths, bool replace);
	Track readTrack(const QString &path) const;

	const QVector<Track> &shown() const
	{
		return showLibrary_ ? library_ : tracks_;
	}
	QString currentPath() const;
	QString numberText(int row, bool isCurrent) const;

	void rebuildTable();
	void markCurrent();
	void applySearch();
	void updateNowPlaying();

	void loadState();
	void saveState() const;

	static QString formatTime(qint64 ms);

	QString configFile_;
	QVector<Track> tracks_;  // lista actual (la que suena)
	QVector<Track> library_; // todas las canciones que has cargado alguna vez
	int cur_ = -1;           // posición en tracks_
	int curRow_ = -1;        // fila de la tabla donde está la canción actual
	int eqFrame_ = 0;
	bool showLibrary_ = false;

	obs_source *source_ = nullptr;
	bool sawPlaying_ = false;
	bool wasPlaying_ = false;
	qint64 lastPos_ = 0;
	qint64 lastDur_ = 0;
	qint64 pendingSeekMs_ = -1;
	int seekTries_ = 0;
	QElapsedTimer seekTimer_;
	bool loading_ = false;
	bool shutdown_ = false;

	QTimer timer_;

	QLabel *nowTitle_ = nullptr;
	QLabel *nowArtist_ = nullptr;
	QLabel *nowAlbum_ = nullptr;
	QLabel *timeLbl_ = nullptr;
	QLabel *durLbl_ = nullptr;
	QSlider *seekSlider_ = nullptr;
	QSlider *volumeSlider_ = nullptr;
	QPushButton *prevBtn_ = nullptr;
	QPushButton *playBtn_ = nullptr;
	QPushButton *nextBtn_ = nullptr;
	QPushButton *stopBtn_ = nullptr;
	QPushButton *tabListBtn_ = nullptr;
	QPushButton *tabLibBtn_ = nullptr;
	QPushButton *tabSearchBtn_ = nullptr;
	QLineEdit *searchEdit_ = nullptr;
	QCheckBox *repeatChk_ = nullptr;
	QCheckBox *monitorChk_ = nullptr;
	QTableWidget *table_ = nullptr;
};

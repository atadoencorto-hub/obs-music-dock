#include "music-dock.hpp"

#include <obs.h>

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QDir>
#include <QDirIterator>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPushButton>
#include <QSlider>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

// TagLib (lectura de metadatos)
#include <fileref.h>
#include <tag.h>
#include <audioproperties.h>
#include <tstring.h>

// Canal de salida de OBS donde "enchufamos" el reproductor (alto para no
// chocar con los dispositivos de audio de OBS, que usan los canales 1-6).
static constexpr int kOutputChannel = 32;
static const char *kSourceName = "Reproductor de música";

static const QStringList kExtensions = {"mp3", "flac", "ogg", "oga", "opus",
					"wav", "m4a",  "aac", "wma", "aif",
					"aiff"};

// Frames del "ecualizador" animado que aparece junto a la canción actual
static const char *kEqFrames[3] = {"▂▄▆", "▄▆▂", "▆▂▄"};

static const QColor kGreen("#2ee04f");
static const QColor kGreenText("#04210b");
static const QColor kNormalText("#e8f0ff");

// ---------------------------------------------------------------------------
// Estilo (tema oscuro azul con acento verde)
// ---------------------------------------------------------------------------
static const char *kStyle = R"CSS(
MusicDock { background: #0a1424; }
QLabel { color: #e8f0ff; background: transparent; }
QLabel#header { color: #4da3ff; font-size: 13pt; font-weight: 800; }
QLabel#nowTitle { font-size: 16pt; font-weight: 800; }
QLabel#nowArtist { font-size: 11pt; font-weight: 600; color: #4da3ff; }
QLabel#nowAlbum, QLabel#time { color: #8fa6c8; font-size: 10pt; }

QFrame#card {
  background: #0f1d33;
  border: 1px solid #1b2f4f;
  border-radius: 14px;
}

QPushButton {
  background: #12233d; color: #e8f0ff;
  border: 1px solid #1f3a5f; border-radius: 10px;
  padding: 8px 12px; font-size: 10pt; font-weight: 600;
}
QPushButton:hover { background: #18304f; border-color: #2d5a94; }
QPushButton:pressed { background: #0f1c30; }
QPushButton:checked, QPushButton#primary {
  background: #1e7bff; border-color: #3f93ff; color: #ffffff;
}
QPushButton#primary:hover { background: #3b8cff; }

QPushButton#transport {
  font-size: 16pt; padding: 2px; min-width: 46px; min-height: 40px;
  border-radius: 12px;
}
QPushButton#playBtn {
  font-size: 18pt; padding: 2px; min-width: 56px; min-height: 40px;
  border-radius: 12px; background: #2ee04f; border-color: #6bf184;
  color: #04210b;
}
QPushButton#playBtn:hover { background: #4cea69; }

QLineEdit {
  background: #0c192d; color: #e8f0ff; border: 1px solid #1f3a5f;
  border-radius: 10px; padding: 8px 12px; font-size: 10pt;
  selection-background-color: #1e7bff;
}
QLineEdit:focus { border-color: #3f93ff; }

QTableWidget {
  background: #0c192d; alternate-background-color: #0e1d34;
  color: #e8f0ff; border: 1px solid #1b2f4f; border-radius: 10px;
  font-size: 11pt; outline: 0;
  selection-background-color: #1c4a8a; selection-color: #ffffff;
}
QTableWidget::item { padding-left: 6px; border: none; }
QHeaderView::section {
  background: #0f1d33; color: #4da3ff; border: none;
  padding: 8px 6px; font-weight: 700; font-size: 10pt;
}
QTableCornerButton::section { background: #0f1d33; border: none; }

QScrollBar:vertical { background: transparent; width: 9px; margin: 2px; }
QScrollBar::handle:vertical {
  background: #1e7bff; border-radius: 4px; min-height: 30px;
}
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {
  background: transparent;
}

QSlider::groove:horizontal {
  height: 6px; background: #1b2f4f; border-radius: 3px;
}
QSlider::sub-page:horizontal { background: #1e7bff; border-radius: 3px; }
QSlider::handle:horizontal {
  background: #ffffff; width: 14px; margin: -5px 0; border-radius: 7px;
}

QCheckBox { color: #c5d4ee; spacing: 8px; font-size: 10pt; }
)CSS";

static bool isSupported(const QString &path)
{
	return kExtensions.contains(QFileInfo(path).suffix().toLower());
}

static int indexOfPath(const QVector<Track> &v, const QString &path)
{
	for (int i = 0; i < v.size(); ++i)
		if (v[i].path == path)
			return i;
	return -1;
}

// ---------------------------------------------------------------------------
// Construcción / destrucción
// ---------------------------------------------------------------------------

MusicDock::MusicDock(const QString &configFile, QWidget *parent)
	: QWidget(parent), configFile_(configFile)
{
	setAcceptDrops(true);
	buildUi();

	timer_.setInterval(250);
	connect(&timer_, &QTimer::timeout, this, &MusicDock::onTick);
	timer_.start();

	loadState();
}

MusicDock::~MusicDock()
{
	shutdown();
}

void MusicDock::shutdown()
{
	if (shutdown_)
		return;
	shutdown_ = true;

	timer_.stop();
	saveState();

	if (source_) {
		obs_source_media_stop(source_);
		obs_set_output_source(kOutputChannel, nullptr);
		obs_source_release(source_);
		source_ = nullptr;
	}
}

// ---------------------------------------------------------------------------
// Interfaz
// ---------------------------------------------------------------------------

void MusicDock::buildUi()
{
	setMinimumWidth(420);
	setAttribute(Qt::WA_StyledBackground, true);
	setStyleSheet(QString::fromUtf8(kStyle));

	auto *root = new QVBoxLayout(this);
	root->setContentsMargins(10, 10, 10, 10);
	root->setSpacing(10);

	// =========================== Tarjeta: reproductor ======================
	auto *playerCard = new QFrame();
	playerCard->setObjectName("card");
	auto *pl = new QVBoxLayout(playerCard);
	pl->setContentsMargins(14, 12, 14, 12);
	pl->setSpacing(6);

	nowTitle_ = new QLabel();
	nowTitle_->setObjectName("nowTitle");
	nowTitle_->setWordWrap(true);
	nowArtist_ = new QLabel();
	nowArtist_->setObjectName("nowArtist");
	nowArtist_->setWordWrap(true);
	nowAlbum_ = new QLabel();
	nowAlbum_->setObjectName("nowAlbum");
	nowAlbum_->setWordWrap(true);
	pl->addWidget(nowTitle_);
	pl->addWidget(nowArtist_);
	pl->addWidget(nowAlbum_);

	auto *seekRow = new QHBoxLayout();
	timeLbl_ = new QLabel("0:00");
	timeLbl_->setObjectName("time");
	durLbl_ = new QLabel("0:00");
	durLbl_->setObjectName("time");
	seekSlider_ = new QSlider(Qt::Horizontal);
	seekSlider_->setRange(0, 0);
	seekRow->addWidget(timeLbl_);
	seekRow->addWidget(seekSlider_, 1);
	seekRow->addWidget(durLbl_);
	pl->addLayout(seekRow);
	connect(seekSlider_, &QSlider::sliderReleased, this,
		&MusicDock::onSeekReleased);

	auto *ctrlRow = new QHBoxLayout();
	ctrlRow->setSpacing(8);

	auto makeBtn = [](const QString &text, const QString &tip,
			  const char *objName) {
		auto *b = new QPushButton(text);
		b->setToolTip(tip);
		b->setObjectName(objName);
		b->setCursor(Qt::PointingHandCursor);
		return b;
	};
	prevBtn_ = makeBtn("⏮", tr("Anterior"), "transport");
	playBtn_ = makeBtn("▶", tr("Reproducir / Pausa"), "playBtn");
	nextBtn_ = makeBtn("⏭", tr("Siguiente"), "transport");
	stopBtn_ = makeBtn("⏹", tr("Detener"), "transport");

	ctrlRow->addWidget(prevBtn_);
	ctrlRow->addWidget(playBtn_);
	ctrlRow->addWidget(nextBtn_);
	ctrlRow->addWidget(stopBtn_);
	ctrlRow->addSpacing(8);
	ctrlRow->addWidget(new QLabel("🔊"));

	volumeSlider_ = new QSlider(Qt::Horizontal);
	volumeSlider_->setRange(0, 100);
	volumeSlider_->setValue(100);
	ctrlRow->addWidget(volumeSlider_, 1);
	pl->addLayout(ctrlRow);

	connect(prevBtn_, &QPushButton::clicked, this, &MusicDock::onPrev);
	connect(playBtn_, &QPushButton::clicked, this, &MusicDock::onPlayPause);
	connect(nextBtn_, &QPushButton::clicked, this, &MusicDock::onNext);
	connect(stopBtn_, &QPushButton::clicked, this, &MusicDock::onStop);
	connect(volumeSlider_, &QSlider::valueChanged, this,
		&MusicDock::onVolumeChanged);

	root->addWidget(playerCard);

	// =========================== Tarjeta: biblioteca =======================
	auto *libCard = new QFrame();
	libCard->setObjectName("card");
	auto *ll = new QVBoxLayout(libCard);
	ll->setContentsMargins(14, 12, 14, 12);
	ll->setSpacing(8);

	auto *header = new QLabel("BIBLIOTECA MUSICAL");
	header->setObjectName("header");
	ll->addWidget(header);

	// Pestañas
	auto *tabRow = new QHBoxLayout();
	tabRow->setSpacing(6);
	tabListBtn_ = new QPushButton("☰  Lista actual");
	tabLibBtn_ = new QPushButton("♫  Todas las canciones");
	tabSearchBtn_ = new QPushButton("🔍  Buscar");
	for (QPushButton *b : {tabListBtn_, tabLibBtn_, tabSearchBtn_}) {
		b->setCheckable(true);
		b->setCursor(Qt::PointingHandCursor);
		tabRow->addWidget(b);
	}
	tabRow->addStretch(1);
	tabListBtn_->setChecked(true);
	ll->addLayout(tabRow);

	auto *group = new QButtonGroup(this);
	group->setExclusive(true);
	group->addButton(tabListBtn_);
	group->addButton(tabLibBtn_);

	connect(tabListBtn_, &QPushButton::clicked, this, [this]() {
		showLibrary_ = false;
		rebuildTable();
	});
	connect(tabLibBtn_, &QPushButton::clicked, this, [this]() {
		showLibrary_ = true;
		rebuildTable();
	});

	searchEdit_ = new QLineEdit();
	searchEdit_->setPlaceholderText(
		tr("Buscar por título, artista o álbum…"));
	searchEdit_->setClearButtonEnabled(true);
	searchEdit_->hide();
	ll->addWidget(searchEdit_);

	connect(tabSearchBtn_, &QPushButton::toggled, this, [this](bool on) {
		searchEdit_->setVisible(on);
		if (on)
			searchEdit_->setFocus();
		applySearch();
	});
	connect(searchEdit_, &QLineEdit::textChanged, this,
		[this](const QString &) { applySearch(); });

	// Tabla
	table_ = new QTableWidget(0, 5);
	table_->setHorizontalHeaderLabels(
		{"#", tr("Título"), tr("Artista"), tr("Álbum"), tr("Duración")});
	table_->setSelectionBehavior(QAbstractItemView::SelectRows);
	table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
	table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
	table_->setAlternatingRowColors(true);
	table_->setShowGrid(false);
	table_->setWordWrap(false);
	table_->setTextElideMode(Qt::ElideRight);
	table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	table_->verticalHeader()->hide();
	table_->verticalHeader()->setDefaultSectionSize(38);
	table_->setMinimumHeight(220);

	auto *hh = table_->horizontalHeader();
	hh->setHighlightSections(false);
	hh->setSectionResizeMode(0, QHeaderView::Fixed);
	hh->setSectionResizeMode(1, QHeaderView::Stretch);
	hh->setSectionResizeMode(2, QHeaderView::Stretch);
	hh->setSectionResizeMode(3, QHeaderView::Stretch);
	hh->setSectionResizeMode(4, QHeaderView::ResizeToContents);
	table_->setColumnWidth(0, 78);

	connect(table_, &QTableWidget::cellDoubleClicked, this,
		&MusicDock::onRowDoubleClicked);
	ll->addWidget(table_, 1);

	// Botones de la lista
	auto *grid = new QGridLayout();
	grid->setSpacing(8);
	auto *loadBtn = new QPushButton("📂  Cargar archivos");
	loadBtn->setObjectName("primary");
	auto *addBtn = new QPushButton("＋  Agregar a la lista");
	auto *removeBtn = new QPushButton("🗑  Eliminar");
	auto *clearBtn = new QPushButton("☰  Limpiar lista");
	for (QPushButton *b : {loadBtn, addBtn, removeBtn, clearBtn})
		b->setCursor(Qt::PointingHandCursor);
	loadBtn->setToolTip(tr("Sustituye la lista actual por los archivos que elijas"));
	addBtn->setToolTip(tr("Añade archivos al final de la lista actual"));
	removeBtn->setToolTip(tr("Quita las canciones seleccionadas"));
	clearBtn->setToolTip(tr("Vacía la lista actual"));
	grid->addWidget(loadBtn, 0, 0);
	grid->addWidget(addBtn, 0, 1);
	grid->addWidget(removeBtn, 1, 0);
	grid->addWidget(clearBtn, 1, 1);
	ll->addLayout(grid);

	connect(loadBtn, &QPushButton::clicked, this,
		[this]() { loadFiles(true); });
	connect(addBtn, &QPushButton::clicked, this,
		[this]() { loadFiles(false); });
	connect(removeBtn, &QPushButton::clicked, this,
		&MusicDock::removeSelected);
	connect(clearBtn, &QPushButton::clicked, this, &MusicDock::clearAll);

	root->addWidget(libCard, 1);

	// =========================== Opciones ==================================
	auto *optRow = new QHBoxLayout();
	repeatChk_ = new QCheckBox(tr("Repetir lista"));
	monitorChk_ = new QCheckBox(tr("Escuchar en mis altavoces"));
	monitorChk_->setChecked(true);
	monitorChk_->setToolTip(
		tr("Además de enviarla al directo/grabación, la música "
		   "se oye por el dispositivo de monitoreo de OBS."));
	optRow->addWidget(repeatChk_);
	optRow->addWidget(monitorChk_);
	optRow->addStretch(1);
	root->addLayout(optRow);

	connect(repeatChk_, &QCheckBox::toggled, this, [this](bool) {
		if (!loading_)
			saveState();
	});
	connect(monitorChk_, &QCheckBox::toggled, this,
		&MusicDock::onMonitorToggled);

	updateNowPlaying();
}

// ---------------------------------------------------------------------------
// Fuente de audio de OBS
// ---------------------------------------------------------------------------

void MusicDock::ensureSource()
{
	if (source_ || shutdown_)
		return;

	obs_data_t *s = obs_data_create();
	obs_data_set_bool(s, "is_local_file", true);
	obs_data_set_bool(s, "looping", false);
	obs_data_set_bool(s, "close_when_inactive", false);
	obs_data_set_bool(s, "restart_on_activate", false);
	obs_data_set_bool(s, "clear_on_media_end", false);

	// Fuente PÚBLICA: así aparece en el Mezclador de audio de OBS.
	// Si OBS ya guardó una con este nombre en la colección, se reutiliza.
	source_ = obs_get_source_by_name(kSourceName);
	if (source_ && std::strcmp(obs_source_get_id(source_), "ffmpeg_source") != 0) {
		obs_source_release(source_);
		source_ = nullptr;
	}
	if (source_)
		obs_source_update(source_, s);
	else
		source_ = obs_source_create("ffmpeg_source", kSourceName, s, nullptr);
	obs_data_release(s);
	if (!source_) {
		blog(LOG_WARNING,
		     "[obs-music-dock] No se pudo crear la fuente multimedia");
		return;
	}

	obs_source_set_audio_mixers(source_, 0x3F); // pistas 1-6
	obs_source_set_volume(source_,
			      std::pow(volumeSlider_->value() / 100.0f, 3.0f));
	obs_source_set_monitoring_type(source_,
				       monitorChk_->isChecked()
					       ? OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT
					       : OBS_MONITORING_TYPE_NONE);

	// Al conectarla a un canal de salida, el audio entra en la mezcla de OBS
	obs_set_output_source(kOutputChannel, source_);
}

void MusicDock::playIndex(int row)
{
	if (row < 0 || row >= tracks_.size())
		return;

	ensureSource();
	if (!source_)
		return;

	cur_ = row;
	sawPlaying_ = false;

	obs_data_t *s = obs_data_create();
	obs_data_set_string(
		s, "local_file",
		QDir::toNativeSeparators(tracks_[row].path).toUtf8().constData());
	obs_source_update(source_, s);
	obs_data_release(s);
	obs_source_media_restart(source_);

	updateNowPlaying();
	markCurrent();
	if (!loading_)
		saveState();
}

void MusicDock::stopPlayback()
{
	sawPlaying_ = false;
	if (source_)
		obs_source_media_stop(source_);
	seekSlider_->setValue(0);
}

void MusicDock::advance(bool automatic)
{
	if (tracks_.isEmpty())
		return;

	int next = cur_ + 1;
	if (next >= tracks_.size()) {
		if (repeatChk_->isChecked()) {
			next = 0;
		} else {
			if (automatic)
				stopPlayback();
			return;
		}
	}
	playIndex(next);
}

// ---------------------------------------------------------------------------
// Controles
// ---------------------------------------------------------------------------

void MusicDock::onPlayPause()
{
	if (tracks_.isEmpty())
		return;

	if (!source_ || cur_ < 0) {
		QList<QTableWidgetItem *> sel = table_->selectedItems();
		int row = sel.isEmpty() ? 0 : sel.first()->row();
		if (showLibrary_ && row < library_.size()) {
			int idx = indexOfPath(tracks_, library_[row].path);
			row = idx >= 0 ? idx : 0;
		}
		playIndex(row);
		return;
	}

	switch (obs_source_media_get_state(source_)) {
	case OBS_MEDIA_STATE_PLAYING:
		obs_source_media_play_pause(source_, true);
		break;
	case OBS_MEDIA_STATE_PAUSED:
		obs_source_media_play_pause(source_, false);
		break;
	default:
		playIndex(cur_);
		break;
	}
}

void MusicDock::onStop()
{
	stopPlayback();
}

void MusicDock::onNext()
{
	advance(false);
}

void MusicDock::onPrev()
{
	if (tracks_.isEmpty())
		return;

	// Si ya sonaron más de 3 s, "Anterior" reinicia la canción actual
	if (source_ && cur_ >= 0 && obs_source_media_get_time(source_) > 3000) {
		obs_source_media_set_time(source_, 0);
		return;
	}

	int prev = cur_ - 1;
	if (prev < 0)
		prev = repeatChk_->isChecked() ? int(tracks_.size()) - 1 : 0;
	playIndex(prev);
}

void MusicDock::onRowDoubleClicked(int row, int)
{
	const QVector<Track> &v = shown();
	if (row < 0 || row >= v.size())
		return;

	if (!showLibrary_) {
		playIndex(row);
		return;
	}

	// Desde "Todas las canciones": la añade a la lista actual y la reproduce
	const Track t = v[row];
	int idx = indexOfPath(tracks_, t.path);
	if (idx < 0) {
		tracks_.append(t);
		idx = int(tracks_.size()) - 1;
	}
	playIndex(idx);
}

void MusicDock::onSeekReleased()
{
	if (source_)
		obs_source_media_set_time(source_, seekSlider_->value());
}

void MusicDock::onVolumeChanged(int value)
{
	if (source_)
		obs_source_set_volume(source_,
				      std::pow(value / 100.0f, 3.0f));
	if (!loading_)
		saveState();
}

void MusicDock::onMonitorToggled(bool checked)
{
	if (source_)
		obs_source_set_monitoring_type(
			source_, checked ? OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT
					 : OBS_MONITORING_TYPE_NONE);
	if (!loading_)
		saveState();
}

void MusicDock::onTick()
{
	if (!source_)
		return;

	// Si OBS eliminó la fuente (p. ej. al cambiar de colección), se suelta;
	// se volverá a crear al pulsar play.
	if (obs_source_removed(source_)) {
		obs_source_release(source_);
		source_ = nullptr;
		playBtn_->setText("▶");
		return;
	}
	// Si el canal de salida se vació, se vuelve a conectar
	obs_source_t *out = obs_get_output_source(kOutputChannel);
	if (out != source_)
		obs_set_output_source(kOutputChannel, source_);
	if (out)
		obs_source_release(out);

	const obs_media_state st = obs_source_media_get_state(source_);
	const bool playing = (st == OBS_MEDIA_STATE_PLAYING);
	playBtn_->setText(playing ? "⏸" : "▶");

	if (playing)
		sawPlaying_ = true;

	const qint64 dur = obs_source_media_get_duration(source_);
	const qint64 pos = obs_source_media_get_time(source_);

	if (dur > 0 && !seekSlider_->isSliderDown()) {
		seekSlider_->setRange(0, int(dur));
		seekSlider_->setValue(int(pos));
	}
	if (!seekSlider_->isSliderDown())
		timeLbl_->setText(formatTime(pos));
	durLbl_->setText(formatTime(dur));

	// Animación del ecualizador junto a la canción actual
	if (playing && curRow_ >= 0 && curRow_ < table_->rowCount()) {
		eqFrame_ = (eqFrame_ + 1) % 3;
		if (QTableWidgetItem *n = table_->item(curRow_, 0))
			n->setText(numberText(curRow_, true));
	}

	// Fin de canción -> siguiente
	if (st == OBS_MEDIA_STATE_ENDED && sawPlaying_) {
		sawPlaying_ = false;
		advance(true);
	}
}

// ---------------------------------------------------------------------------
// Lista de reproducción, biblioteca y metadatos
// ---------------------------------------------------------------------------

void MusicDock::loadFiles(bool replace)
{
	const QStringList files = QFileDialog::getOpenFileNames(
		this, replace ? tr("Cargar canciones") : tr("Agregar canciones"),
		QString(),
		tr("Audio (*.mp3 *.flac *.ogg *.oga *.opus *.wav *.m4a *.aac "
		   "*.wma *.aif *.aiff)"));
	if (!files.isEmpty())
		addPaths(files, replace);
}

void MusicDock::addPaths(const QStringList &paths, bool replace)
{
	QStringList files;

	for (const QString &p : paths) {
		const QFileInfo fi(p);
		if (fi.isDir()) {
			QStringList found;
			QStringList filters;
			for (const QString &e : kExtensions)
				filters << "*." + e;
			QDirIterator it(p, filters, QDir::Files,
					QDirIterator::Subdirectories);
			while (it.hasNext())
				found << it.next();
			found.sort(Qt::CaseInsensitive);
			files << found;
		} else if (fi.isFile() && isSupported(p)) {
			files << p;
		}
	}

	if (files.isEmpty())
		return;

	if (replace) {
		stopPlayback();
		tracks_.clear();
		cur_ = -1;
		updateNowPlaying();
	}

	QApplication::setOverrideCursor(Qt::WaitCursor);
	for (const QString &f : files) {
		Track t;
		const int li = indexOfPath(library_, f);
		if (li >= 0) {
			t = library_[li];
		} else {
			t = readTrack(f);
			library_.append(t);
		}
		tracks_.append(t);
	}
	QApplication::restoreOverrideCursor();

	rebuildTable();
	if (!loading_)
		saveState();
}

Track MusicDock::readTrack(const QString &path) const
{
	Track t;
	t.path = path;
	t.title = QFileInfo(path).completeBaseName();
	t.artist = tr("Desconocido");
	t.album = tr("Desconocido");

#ifdef _WIN32
	const std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
	TagLib::FileRef f(wpath.c_str());
#else
	const QByteArray bpath = QFile::encodeName(path);
	TagLib::FileRef f(bpath.constData());
#endif

	if (!f.isNull()) {
		if (TagLib::Tag *tag = f.tag()) {
			const QString title =
				QString::fromUtf8(tag->title().toCString(true))
					.trimmed();
			const QString artist =
				QString::fromUtf8(tag->artist().toCString(true))
					.trimmed();
			const QString album =
				QString::fromUtf8(tag->album().toCString(true))
					.trimmed();
			if (!title.isEmpty())
				t.title = title;
			if (!artist.isEmpty())
				t.artist = artist;
			if (!album.isEmpty())
				t.album = album;
		}
		if (f.audioProperties())
			t.durationMs = f.audioProperties()->lengthInMilliseconds();
	}
	return t;
}

QString MusicDock::currentPath() const
{
	return (cur_ >= 0 && cur_ < tracks_.size()) ? tracks_[cur_].path
						    : QString();
}

QString MusicDock::numberText(int row, bool isCurrent) const
{
	if (!isCurrent)
		return QString::number(row + 1);
	return QString("%1   %2")
		.arg(row + 1)
		.arg(QString::fromUtf8(kEqFrames[eqFrame_ % 3]));
}

void MusicDock::rebuildTable()
{
	const QVector<Track> &v = shown();

	table_->setRowCount(0);
	table_->setRowCount(int(v.size()));

	for (int r = 0; r < v.size(); ++r) {
		const Track &t = v[r];
		const QString cols[5] = {QString::number(r + 1), t.title,
					 t.artist, t.album,
					 formatTime(t.durationMs)};
		for (int c = 0; c < 5; ++c) {
			auto *item = new QTableWidgetItem(cols[c]);
			item->setToolTip(QString("%1\n%2 — %3")
						 .arg(t.title, t.artist, t.album));
			if (c == 0)
				item->setTextAlignment(Qt::AlignLeft |
						       Qt::AlignVCenter);
			if (c == 4)
				item->setTextAlignment(Qt::AlignRight |
						       Qt::AlignVCenter);
			table_->setItem(r, c, item);
		}
	}
	markCurrent();
	applySearch();
}

void MusicDock::markCurrent()
{
	const QVector<Track> &v = shown();
	const QString cp = currentPath();
	curRow_ = -1;

	for (int r = 0; r < table_->rowCount() && r < v.size(); ++r) {
		const bool isCur = !cp.isEmpty() && v[r].path == cp;
		if (isCur)
			curRow_ = r;

		for (int c = 0; c < 5; ++c) {
			QTableWidgetItem *item = table_->item(r, c);
			if (!item)
				continue;
			QFont f = item->font();
			f.setBold(isCur);
			item->setFont(f);
			if (isCur) {
				item->setBackground(kGreen);
				item->setForeground(kGreenText);
			} else {
				item->setBackground(QBrush());
				item->setForeground(kNormalText);
			}
		}
		if (QTableWidgetItem *n = table_->item(r, 0))
			n->setText(numberText(r, isCur));
	}
}

void MusicDock::applySearch()
{
	const bool active = tabSearchBtn_ && tabSearchBtn_->isChecked();
	const QString q = active ? searchEdit_->text().trimmed() : QString();
	const QVector<Track> &v = shown();

	for (int r = 0; r < table_->rowCount() && r < v.size(); ++r) {
		bool match = true;
		if (!q.isEmpty()) {
			const Track &t = v[r];
			match = t.title.contains(q, Qt::CaseInsensitive) ||
				t.artist.contains(q, Qt::CaseInsensitive) ||
				t.album.contains(q, Qt::CaseInsensitive);
		}
		table_->setRowHidden(r, !match);
	}
}

void MusicDock::updateNowPlaying()
{
	if (cur_ >= 0 && cur_ < tracks_.size()) {
		const Track &t = tracks_[cur_];
		nowTitle_->setText(t.title);
		nowArtist_->setText(t.artist);
		nowAlbum_->setText(t.album);
	} else {
		nowTitle_->setText(tr("Nada en reproducción"));
		nowArtist_->setText(tr("Carga canciones para empezar"));
		nowAlbum_->clear();
	}
}

void MusicDock::removeSelected()
{
	QList<int> rows;
	for (const QModelIndex &idx : table_->selectionModel()->selectedRows())
		rows << idx.row();
	if (rows.isEmpty())
		return;

	std::sort(rows.begin(), rows.end(), std::greater<int>());

	if (showLibrary_) {
		// En "Todas las canciones" solo se quitan de la biblioteca
		for (int r : rows)
			if (r < library_.size())
				library_.remove(r);
	} else {
		for (int r : rows) {
			if (r >= tracks_.size())
				continue;
			if (r == cur_) {
				stopPlayback();
				cur_ = -1;
			} else if (r < cur_) {
				--cur_;
			}
			tracks_.remove(r);
		}
	}

	rebuildTable();
	updateNowPlaying();
	saveState();
}

void MusicDock::clearAll()
{
	stopPlayback();
	tracks_.clear();
	cur_ = -1;
	rebuildTable();
	updateNowPlaying();
	saveState();
}

// ---------------------------------------------------------------------------
// Arrastrar y soltar
// ---------------------------------------------------------------------------

void MusicDock::dragEnterEvent(QDragEnterEvent *e)
{
	if (e->mimeData()->hasUrls())
		e->acceptProposedAction();
}

void MusicDock::dropEvent(QDropEvent *e)
{
	QStringList paths;
	for (const QUrl &u : e->mimeData()->urls())
		if (u.isLocalFile())
			paths << u.toLocalFile();
	addPaths(paths, false);
	e->acceptProposedAction();
}

// ---------------------------------------------------------------------------
// Guardado de la lista y opciones
// ---------------------------------------------------------------------------

void MusicDock::saveState() const
{
	if (loading_ || configFile_.isEmpty())
		return;

	QJsonArray list, lib;
	for (const Track &t : tracks_)
		list.append(t.path);
	for (const Track &t : library_)
		lib.append(t.path);

	QJsonObject o;
	o["tracks"] = list;
	o["library"] = lib;
	o["volume"] = volumeSlider_->value();
	o["repeat"] = repeatChk_->isChecked();
	o["monitor"] = monitorChk_->isChecked();

	QDir().mkpath(QFileInfo(configFile_).absolutePath());
	QFile f(configFile_);
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}

void MusicDock::loadState()
{
	if (configFile_.isEmpty())
		return;

	QFile f(configFile_);
	if (!f.open(QIODevice::ReadOnly))
		return;

	const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();

	loading_ = true;
	volumeSlider_->setValue(o.value("volume").toInt(100));
	repeatChk_->setChecked(o.value("repeat").toBool(false));
	monitorChk_->setChecked(o.value("monitor").toBool(true));

	for (const QJsonValue &v : o.value("library").toArray()) {
		const QString p = v.toString();
		if (QFileInfo::exists(p) && indexOfPath(library_, p) < 0)
			library_.append(readTrack(p));
	}
	for (const QJsonValue &v : o.value("tracks").toArray()) {
		const QString p = v.toString();
		if (!QFileInfo::exists(p))
			continue;
		int li = indexOfPath(library_, p);
		if (li < 0) {
			library_.append(readTrack(p));
			li = int(library_.size()) - 1;
		}
		tracks_.append(library_[li]);
	}

	rebuildTable();
	loading_ = false;
}

// ---------------------------------------------------------------------------
// Utilidades
// ---------------------------------------------------------------------------

QString MusicDock::formatTime(qint64 ms)
{
	if (ms < 0)
		ms = 0;
	const qint64 total = ms / 1000;
	const qint64 h = total / 3600;
	const qint64 m = (total % 3600) / 60;
	const qint64 s = total % 60;

	if (h > 0)
		return QString("%1:%2:%3")
			.arg(h)
			.arg(m, 2, 10, QChar('0'))
			.arg(s, 2, 10, QChar('0'));
	return QString("%1:%2").arg(m, 2, 10, QChar('0'))
		.arg(s, 2, 10, QChar('0'));
}

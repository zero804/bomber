/*
 * Copyright (C) 2007-2008 John-Paul Stanford <jp@stanwood.org.uk>
 * Copyright 2010 Stefan Majewsky <majewsky@gmx.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

// own
#include "bomberwidget.h"

// Qt
#include <QGraphicsItem>
#include <QTimer>

// KDE
#include <KLocalizedString>

// Bomber
#include "settings.h"

#define NEW_LIVE_AT_SCORE 10000;

/** The amount the score increases when a bomb hits something */
static const unsigned int SCORE_INCREMENT = 5;

static const unsigned int GAME_TIME_DELAY = 1000;
static const unsigned int TICKS_PER_SECOND = 1000 / GAME_TIME_DELAY;
/** The z-value for overlays */
static const unsigned int OVERLAY_Z_VALUE = 1000;

BomberGameWidget::BomberGameWidget(KgThemeProvider * provider, QWidget * parent)
    : QGraphicsView(parent)
    , m_state(State::BeforeFirstGame)
    , m_level(0)
    , m_lives(0)
    , m_time(0)
    , m_renderer(provider)
    , m_soundBomb(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("bomber/sounds/bomb.ogg")))
    , m_soundCrash(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("bomber/sounds/crash.ogg")))
{
    // Gameboard
    m_board = new BomberBoard(&m_renderer, this, this);
    connect(m_board, &BomberBoard::playBombSound, this, &BomberGameWidget::playBombSound);
    connect(m_board, &BomberBoard::playCrashSound, this, &BomberGameWidget::playCrashSound);
    connect(m_board, &BomberBoard::onPlaneCrash, this, &BomberGameWidget::onPlaneCrashed);
    connect(m_board, &BomberBoard::onBombHit, this, &BomberGameWidget::onBombHit);
    connect(m_board, &BomberBoard::levelCleared, this, &BomberGameWidget::onLevelCleared);
    setScene(m_board);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameStyle(QFrame::NoFrame);
    setCacheMode(QGraphicsView::CacheBackground); // Optimize caching
    setFocusPolicy(Qt::StrongFocus);
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    // Overlay
    m_overlay = new QGraphicsPixmapItem();

    m_board->addItem(m_overlay);

    m_clock = new QTimer(this);
    m_clock->setInterval(GAME_TIME_DELAY);

    connect(m_clock, &QTimer::timeout, this, &BomberGameWidget::tick);

    setMouseTracking(true);
    generateOverlay();
}

BomberGameWidget::~BomberGameWidget()
{
    delete m_board;
}

unsigned int BomberGameWidget::level() const
{
    return m_level;
}

unsigned int BomberGameWidget::score() const
{
    return m_score;
}

void BomberGameWidget::closeGame()
{
    if (m_state != State::BeforeFirstGame && m_state != State::GameOver) {
        closeLevel();

        m_state = State::GameOver;
        emit stateChanged(m_state);
        emit gameOver();

        redraw();
    }
}

void BomberGameWidget::newGame()
{
    closeGame();

    m_level = 1;
    m_score = 0;
    m_lives = 3;
    m_scoreLeftBeforeNewLife = NEW_LIVE_AT_SCORE;

    emit levelChanged(m_level);
    emit scoreChanged(m_score);
    emit livesChanged(m_lives);

    newLevel();
}

void BomberGameWidget::setPaused(bool val)
{
    if (m_state == State::Paused && val == false) {
        m_clock->start();
        m_board->setPaused(false);
        m_state = State::Running;
    } else if (m_state == State::Running && val == true) {
        m_clock->stop();
        m_board->setPaused(true);
        m_state = State::Paused;
    }
    emit stateChanged(m_state);
    redraw();
}

void BomberGameWidget::setSuspended(bool val)
{
    if (m_state == State::Suspended && val == false) {
        m_clock->start();
        m_board->setPaused(false);
        m_state = State::Running;
    } else if (m_state == State::Running && val == true) {
        m_clock->stop();
        m_board->setPaused(true);
        m_state = State::Suspended;
    }
    emit stateChanged(m_state);
    redraw();
}

void BomberGameWidget::settingsChanged()
{
    m_board->settingsChanged();
}

void BomberGameWidget::setSoundsEnabled(bool enable)
{
    BomberSettings::setPlaySounds(enable);
    BomberSettings::self()->save();
    m_board->settingsChanged();
}

void BomberGameWidget::onPlaneCrashed()
{
    --m_lives;
    emit livesChanged(m_lives);
    if (m_lives == 0) {
        closeGame();
    } else {
        m_board->resetPlane();
    }
    if (BomberSettings::playSounds() == true) {
        m_soundCrash.stop();
    }
}

void BomberGameWidget::onBombHit()
{
    unsigned int bonus = SCORE_INCREMENT * m_level;
    m_score += bonus;
    emit scoreChanged(m_score);
    m_scoreLeftBeforeNewLife -= bonus;
    if (m_scoreLeftBeforeNewLife <= 0) {
        m_scoreLeftBeforeNewLife = NEW_LIVE_AT_SCORE;
        ++m_lives;
        emit livesChanged(m_lives);
    }
    if (BomberSettings::playSounds() == true) {
        m_soundBomb.stop();
    }
}

void BomberGameWidget::playBombSound()
{
    if (BomberSettings::playSounds() == true) {
        m_soundBomb.start();
    }
}

void BomberGameWidget::playCrashSound()
{
    if (BomberSettings::playSounds() == true) {
        m_soundCrash.start();
    }
}

void BomberGameWidget::tick()
{
    static signed int ticks = TICKS_PER_SECOND;
    --ticks;
    if (ticks <= 0) {
        ++m_time;
        emit timeChanged(m_time);
        ticks = TICKS_PER_SECOND;
    }
}

void BomberGameWidget::resizeEvent(QResizeEvent * ev)
{
    QSize boardSize = ev->size();
    m_board->resize(boardSize);
    redraw();
}

void BomberGameWidget::closeLevel()
{
    m_clock->stop();
    m_board->setPaused(true);
}

void BomberGameWidget::newLevel()
{
    m_state = State::Running;
    emit stateChanged(m_state);

    m_clock->start();
    m_board->newLevel(m_level);
    m_board->setPaused(false);

    emit livesChanged(m_lives);
    emit timeChanged(m_time);

    redraw();
}

void BomberGameWidget::mouseReleaseEvent(QMouseEvent * event)
{
    if (event->button() & Qt::LeftButton) {
        onDropBomb();
    }
}

void BomberGameWidget::onDropBomb()
{
    if (m_state == State::Running) {
        m_board->dropBomb();
    } else if (m_state == State::BetweenLevels) {
        newLevel();
    } else if (m_state == State::BeforeFirstGame) {
        newGame();
    }
}

void BomberGameWidget::redraw()
{
    if (size().isEmpty()) {
        return;
    }
    QGraphicsItem * item;
    switch (m_state) {
        case State::BeforeFirstGame:
            foreach (item, m_board->items()) {
                item->hide();
            }
            generateOverlay();
            m_overlay->show();
            break;
        case State::Running:
            foreach (item, m_board->items()) {
                item->show();
            }
            m_overlay->hide();
            break;
        default:
            foreach (item, m_board->items()) {
                item->show();
            }
            generateOverlay();
            m_overlay->show();
            break;
    }
    m_board->redraw();
    update();
}

void BomberGameWidget::generateOverlay()
{
    unsigned int itemWidth = qRound(0.8 * size().width());
    unsigned int itemHeight = qRound(0.6 * size().height());

    QPixmap px(itemWidth, itemHeight);
    px.fill(QColor(0, 0, 0, 0));

    QPainter p(&px);
    p.setPen(QColor(0, 0, 0, 0));
    p.setBrush(QBrush(QColor(188, 202, 222, 155)));
    p.setRenderHint(QPainter::Antialiasing);
    p.drawRoundedRect(0, 0, itemWidth, itemHeight, 25, Qt::RelativeSize);

    QString text;
    switch (m_state) {
        case State::BeforeFirstGame:
            text = i18nc("Message show to the user when the game is loaded", "Welcome to Bomber.\nClick to start a game");
            break;
        case State::Paused:
            text = i18nc("Message show to the user while the game is paused", "Paused");
            break;
        case State::BetweenLevels:
            text = i18nc("Message telling user which level they just completed", "You have successfully cleared level %1\n", m_level - 1)
                + i18nc("Message telling user which level they are about to start", "On to level %1.", m_level);
            break;
        case State::GameOver:
            text = i18nc("Used to tell the user that the game is over", "Game over.");
            break;
        default:
            text.clear();
    }

    QFont font;
    font.setPointSize(28);
    p.setFont(font);
    unsigned int textWidth = p.boundingRect(p.viewport(), Qt::AlignCenter | Qt::AlignVCenter, text).width();
    unsigned int fontSize = 28;
    while (textWidth > itemWidth * 0.95) {
        --fontSize;
        font.setPointSize(fontSize);
        p.setFont(font);
        textWidth = p.boundingRect(p.viewport(), Qt::AlignCenter | Qt::AlignVCenter, text).width();
    }

    p.setPen(QColor(0, 0, 0, 255));
    p.drawText(p.viewport(), Qt::AlignCenter | Qt::AlignVCenter, text);
    p.end();

    m_overlay->setPixmap(px);
    m_overlay->setPos((size().width() - itemWidth) / 2, (size().height() - itemHeight) / 2);
    m_overlay->setZValue(OVERLAY_Z_VALUE);
}

void BomberGameWidget::onLevelCleared()
{
    if (m_state == State::Running) {
        m_state = State::BetweenLevels;
        closeLevel();
        ++m_level;
        emit levelChanged(m_level);
        emit stateChanged(m_state);
        redraw();
    }
}

BomberGameWidget::State BomberGameWidget::state() const
{
    return m_state;
}

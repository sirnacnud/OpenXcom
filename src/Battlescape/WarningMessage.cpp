/*
 * Copyright 2010 OpenXcom Developers.
 *
 * This file is part of OpenXcom.
 *
 * OpenXcom is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * OpenXcom is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with OpenXcom.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "WarningMessage.h"
#include "SDL.h"
#include "../Engine/Timer.h"
#include "../Engine/Sound.h"
#include "../Interface/Text.h"

namespace OpenXcom
{

/**
 * Sets up a blank warning message with the specified size and position.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @param x X position in pixels.
 * @param y Y position in pixels.
 */
WarningMessage::WarningMessage(int width, int height, int x, int y) : Surface(width, height, x, y), _color(0), _fade(0)
{
	_text = new Text(width, 9, 0, (height - 8) / 2);
	_text->setHighContrast(true);
	_text->setAlign(ALIGN_CENTER);

	_timer = new Timer(50);
	_timer->onTimer((SurfaceHandler)&WarningMessage::fade);

	setVisible(false);
}

/**
 * Deletes timers.
 */
WarningMessage::~WarningMessage()
{
	delete _timer;
	delete _text;
}

/**
 * Changes the color for the message background.
 * @param color Color value.
 */
void WarningMessage::setColor(Uint8 color)
{
	_color = color;
}

/**
 * Changes the color for the message text.
 * @param color Color value.
 */
void WarningMessage::setTextColor(Uint8 color)
{
	_text->setColor(color);
}

/**
 * Changes the various fonts for the message to use.
 * The different fonts need to be passed in advance since the
 * text size can change mid-text.
 * @param big Pointer to large-size font.
 * @param small Pointer to small-size font.
 */
void WarningMessage::setFonts(Font *big, Font *small)
{
	_text->setFonts(big, small);
}

/**
 * Replaces a certain amount of colors in the surface's palette.
 * @param colors Pointer to the set of colors.
 * @param firstcolor Offset of the first color to replace.
 * @param ncolors Amount of colors to replace.
 */
void WarningMessage::setPalette(SDL_Color *colors, int firstcolor, int ncolors)
{
	Surface::setPalette(colors, firstcolor, ncolors);
	_text->setPalette(colors, firstcolor, ncolors);
}

/**
 * Displays the warning message.
 * @param msg Message string.
 */
void WarningMessage::showMessage(const std::wstring &msg)
{
	_text->setText(msg);
	_fade = 0;
	_redraw = true;
	setVisible(true);
	_timer->start();
}

/**
 * Keeps the animation timers running.
 */
void WarningMessage::think()
{
	_timer->think(0, this);
}

/**
 * Plays the message fade animation.
 */
void WarningMessage::fade()
{
	_fade++;
	_redraw = true;
	if (_fade == 24)
	{
		setVisible(false);
		_timer->stop();
	}
}

/**
 * Draws the warning message.
 */
void WarningMessage::draw()
{
	Surface::draw();

	SDL_Rect square1;
	square1.x = 0;
	square1.y = 0;
	square1.w = getWidth();
	square1.h = getHeight();
	drawRect(&square1, _color + (_fade > 12 ? 12 : _fade));

	_text->blit(this);
}

}

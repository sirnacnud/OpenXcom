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
#define _USE_MATH_DEFINES
#include <cmath>
#include <fstream>
#include "Map.h"
#include "Camera.h"
#include "UnitSprite.h"
#include "Position.h"
#include "Pathfinding.h"
#include "TileEngine.h"
#include "Projectile.h"
#include "BulletSprite.h"
#include "Explosion.h"
#include "../Resource/ResourcePack.h"
#include "../Engine/Action.h"
#include "../Engine/SurfaceSet.h"
#include "../Engine/Timer.h"
#include "../Engine/Font.h"
#include "../Engine/Language.h"
#include "../Engine/Palette.h"
#include "../Engine/RNG.h"
#include "../Engine/Game.h"
#include "../Savegame/SavedBattleGame.h"
#include "../Savegame/Tile.h"
#include "../Savegame/BattleUnit.h"
#include "../Ruleset/MapDataSet.h"
#include "../Ruleset/MapData.h"
#include "../Ruleset/RuleArmor.h"
#include "BattlescapeMessage.h"
#include "../Savegame/SavedGame.h"
#include "../Interface/Cursor.h"
#include "../Engine/Options.h"

/*
  1) Map origin is top corner.
  2) X axis goes downright. (width of the map)
  3) Y axis goes downleft. (length of the map
  4) Z axis goes up (height of the map)

		   0,0
			/\
		y+ /  \ x+
		   \  /
			\/
*/

namespace OpenXcom
{

/**
 * Sets up a map with the specified size and position.
 * @param game Pointer to the core game.
 * @param width Width in pixels.
 * @param height Height in pixels.
 * @param x X position in pixels.
 * @param y Y position in pixels.
 */
Map::Map(Game *game, int width, int height, int x, int y, int visibleMapHeight) : InteractiveSurface(width, height, x, y), _game(game), _selectorX(0), _selectorY(0), _cursorType(CT_NORMAL), _animFrame(0), _visibleMapHeight(visibleMapHeight)
{
	_res = _game->getResourcePack();
	_spriteWidth = _res->getSurfaceSet("BLANKS.PCK")->getFrame(0)->getWidth();
	_spriteHeight = _res->getSurfaceSet("BLANKS.PCK")->getFrame(0)->getHeight();
	_save = _game->getSavedGame()->getBattleGame();
	_message = new BattlescapeMessage(width, visibleMapHeight, 0, 0);
	_camera = new Camera(_spriteWidth, _spriteHeight, _save->getWidth(), _save->getLength(), _save->getHeight(), this, visibleMapHeight);
	_scrollTimer = new Timer(SCROLL_INTERVAL);
	_scrollTimer->onTimer((SurfaceHandler)&Map::scroll);
	_camera->setScrollTimer(_scrollTimer);

}

/**
 * Deletes the map.
 */
Map::~Map()
{
	delete _scrollTimer;

	delete _arrow;

	for (int i = 0; i < 36; ++i)
	{
		delete _bullet[i];
	}
}

/**
 * initializes stuff
 */
void Map::init()
{
	// load the tiny arrow into a surface
	int f = Palette::blockOffset(1)+1; // yellow
	int b = 15; // black
	int pixels[81] = { 0, 0, b, b, b, b, b, 0, 0,
					   0, 0, b, f, f, f, b, 0, 0,
					   0, 0, b, f, f, f, b, 0, 0,
					   b, b, b, f, f, f, b, b, b,
					   b, f, f, f, f, f, f, f, b,
					   0, b, f, f, f, f, f, b, 0,
					   0, 0, b, f, f, f, b, 0, 0,
					   0, 0, 0, b, f, b, 0, 0, 0,
					   0, 0, 0, 0, b, 0, 0, 0, 0 };

	_arrow = new Surface(9, 9);
	_arrow->setPalette(this->getPalette());
	_arrow->lock();
	for (int y = 0; y < 9;++y)
		for (int x = 0; x < 9; ++x)
			_arrow->setPixel(x, y, pixels[x+(y*9)]);
	_arrow->unlock();

	for (int i = 0; i < 36; ++i)
	{
		_bullet[i] = new BulletSprite(i);
		_bullet[i]->setPalette(this->getPalette());
	}

	_projectile = 0;
}

/**
 * Keeps the animation timers running.
 */
void Map::think()
{
	_scrollTimer->think(0, this);
}

/**
 * Draws the whole map, part by part.
 */
void Map::draw()
{
	Surface::draw();
	if ((_save->getSelectedUnit() && _save->getSelectedUnit()->getVisible()) || _save->getSelectedUnit() == 0 || _save->getDebugMode() || _projectile || !_explosions.empty())
	{
		drawTerrain(this);
	}
	else
	{
		_message->blit(this);
	}
}

/**
 * Replaces a certain amount of colors in the surface's palette.
 * @param colors Pointer to the set of colors.
 * @param firstcolor Offset of the first color to replace.
 * @param ncolors Amount of colors to replace.
 */
void Map::setPalette(SDL_Color *colors, int firstcolor, int ncolors)
{
	Surface::setPalette(colors, firstcolor, ncolors);
	for (std::vector<MapDataSet*>::const_iterator i = _save->getMapDataSets()->begin(); i != _save->getMapDataSets()->end(); ++i)
	{
		(*i)->getSurfaceset()->setPalette(colors, firstcolor, ncolors);
	}
	_message->setPalette(colors, firstcolor, ncolors);
	_message->setBackground(_res->getSurface("TAC00.SCR"));
	_message->setFonts(_res->getFont("Big.fnt"), _res->getFont("Small.fnt"));
	_message->setText(_game->getLanguage()->getString("STR_HIDDEN_MOVEMENT"));
}

/**
* Draw the terrain.
* Keep this function as optimised as possible. It's big to minimise overhead of function calls.
* @param surface The surface to draw on.
*/
void Map::drawTerrain(Surface *surface)
{
	int frameNumber = 0;
	Surface *tmpSurface;
	Tile *tile;
	int beginX = 0, endX = _save->getWidth() - 1;
	int beginY = 0, endY = _save->getLength() - 1;
	int beginZ = 0, endZ = _camera->getViewHeight();
	Position mapPosition, screenPosition, bulletPositionScreen;
	int bulletLowX=16000, bulletLowY=16000, bulletLowZ=16000, bulletHighX=0, bulletHighY=0, bulletHighZ=0;
	int dummy;
	BattleUnit *unit = 0;
	bool invalid;
	int tileShade, wallShade;

	// get corner map coordinates to give rough boundaries in which tiles to redraw are
	_camera->convertScreenToMap(0, 0, &beginX, &dummy);
	_camera->convertScreenToMap(surface->getWidth(), 0, &dummy, &beginY);
	_camera->convertScreenToMap(surface->getWidth(), surface->getHeight(), &endX, &dummy);
	_camera->convertScreenToMap(0, surface->getHeight(), &dummy, &endY);
	beginY -= (_camera->getViewHeight() * 2);
	beginX -= (_camera->getViewHeight() * 2);
	if (beginX < 0)
		beginX = 0;
	if (beginY < 0)
		beginY = 0;

	// if we got bullet, get the highest x and y tiles to draw it on
	if (_projectile && !_projectile->getItem())
	{
		for (int i = 1; i <= _projectile->getParticle(0); ++i)
		{
			if (_projectile->getPosition(1-i).x < bulletLowX)
				bulletLowX = _projectile->getPosition(1-i).x;
			if (_projectile->getPosition(1-i).y < bulletLowY)
				bulletLowY = _projectile->getPosition(1-i).y;
			if (_projectile->getPosition(1-i).z < bulletLowZ)
				bulletLowZ = _projectile->getPosition(1-i).z;
			if (_projectile->getPosition(1-i).x > bulletHighX)
				bulletHighX = _projectile->getPosition(1-i).x;
			if (_projectile->getPosition(1-i).y > bulletHighY)
				bulletHighY = _projectile->getPosition(1-i).y;
			if (_projectile->getPosition(1-i).z > bulletHighZ)
				bulletHighZ = _projectile->getPosition(1-i).z;
		}
		// divide by 16 to go from voxel to tile position
		bulletLowX = bulletLowX / 16;
		bulletLowY = bulletLowY / 16;
		bulletLowZ = bulletLowZ / 24;
		bulletHighX = bulletHighX / 16;
		bulletHighY = bulletHighY / 16;
		bulletHighZ = bulletHighZ / 24;

		// if the projectile is outside the viewport - center it back on it
		_camera->convertVoxelToScreen(_projectile->getPosition(), &bulletPositionScreen);
		if (bulletPositionScreen.x < 0 || bulletPositionScreen.x > surface->getWidth() ||
			bulletPositionScreen.y < 0 || bulletPositionScreen.y > _visibleMapHeight  )
		{
			_camera->centerOnPosition(Position(bulletLowX, bulletLowY, bulletLowZ));
		}
	}

	surface->lock();

	for (int itZ = beginZ; itZ <= endZ; itZ++)
	{
		for (int itX = beginX; itX <= endX; itX++)
		{
			for (int itY = beginY; itY <= endY; itY++)
			{
				mapPosition = Position(itX, itY, itZ);
				_camera->convertMapToScreen(mapPosition, &screenPosition);
				screenPosition += _camera->getMapOffset();

				// only render cells that are inside the surface
				if (screenPosition.x > -_spriteWidth && screenPosition.x < surface->getWidth() + _spriteWidth &&
					screenPosition.y > -_spriteHeight && screenPosition.y < surface->getHeight() + _spriteHeight )
				{
					tile = _save->getTile(mapPosition);

					if (tile->isDiscovered(2))
					{
						tileShade = tile->getShade();
					}
					else
					{
						tileShade = 16;
						unit = 0;
					}

					// Draw floor
					tmpSurface = tile->getSprite(MapData::O_FLOOR);
					if (tmpSurface)
						tmpSurface->blitNShade(surface, screenPosition.x, screenPosition.y - tile->getMapData(MapData::O_FLOOR)->getYOffset(), tileShade, false, tile->getMarkerColor());
					unit = tile->getUnit();

					// Draw cursor back
					if (_selectorX == itX && _selectorY == itY && _cursorType != CT_NONE)
					{
						if (_camera->getViewHeight() == itZ)
						{
							if (_cursorType == CT_NORMAL || _cursorType == CT_THROW)
							{
								if (unit && (unit->getVisible() || _save->getDebugMode()))
									frameNumber = 1; // yellow box
								else
									frameNumber = 0; // red box
							}
							if (_cursorType == CT_AIM)
							{
								if (unit)
									frameNumber = 7 + (_animFrame / 2); // yellow animated crosshairs
								else
									frameNumber = 6; // red static crosshairs
							}
						}
						else if (_camera->getViewHeight() > itZ)
						{
							frameNumber = 2; // blue box
						}
						tmpSurface = _res->getSurfaceSet("CURSOR.PCK")->getFrame(frameNumber);
						tmpSurface->blitNShade(surface, screenPosition.x, screenPosition.y, 0);
					}

					// Draw walls
					if (!tile->isVoid() && (tile->isDiscovered(0) || tile->isDiscovered(1)))
					{
						// Draw west wall
						tmpSurface = tile->getSprite(MapData::O_WESTWALL);
						if (tmpSurface)
						{
							if (tile->getMapData(MapData::O_WESTWALL)->isDoor() || tile->getMapData(MapData::O_WESTWALL)->isUFODoor())
								wallShade = 0;
							else
								wallShade = tileShade;
							tmpSurface->blitNShade(surface, screenPosition.x, screenPosition.y - tile->getMapData(MapData::O_WESTWALL)->getYOffset(), wallShade);
						}
						// Draw north wall
						tmpSurface = tile->getSprite(MapData::O_NORTHWALL);
						if (tmpSurface)
						{
							if (tile->getMapData(MapData::O_NORTHWALL)->isDoor() || tile->getMapData(MapData::O_NORTHWALL)->isUFODoor())
								wallShade = 0;
							else
								wallShade = tileShade;
							if (tile->getMapData(MapData::O_WESTWALL))
							{
								tmpSurface->blitNShade(surface, screenPosition.x, screenPosition.y - tile->getMapData(MapData::O_NORTHWALL)->getYOffset(), wallShade, true);
							}
							else
							{
								tmpSurface->blitNShade(surface, screenPosition.x, screenPosition.y - tile->getMapData(MapData::O_NORTHWALL)->getYOffset(), wallShade);
							}
						}
						// Draw object
						if (tile->isDiscovered(2) || (tile->getMapData(MapData::O_OBJECT) && tile->getMapData(MapData::O_OBJECT)->isBigWall() && (tile->isDiscovered(0) || tile->isDiscovered(1))))
						{
							tmpSurface = tile->getSprite(MapData::O_OBJECT);
							if (tmpSurface)
								tmpSurface->blitNShade(surface, screenPosition.x, screenPosition.y - tile->getMapData(MapData::O_OBJECT)->getYOffset(), tileShade);

							// draw an item on top of the floor (if any)
							int sprite = tile->getTopItemSprite();
							if (sprite != -1)
							{
								tmpSurface = _res->getSurfaceSet("FLOOROB.PCK")->getFrame(sprite);
								tmpSurface->blitNShade(surface, screenPosition.x, screenPosition.y + tile->getTerrainLevel(), tileShade);
							}
						}
					}

					// check if we got bullet
					if (_projectile)
					{
						tmpSurface = 0;
						if (_projectile->getItem())
						{
							tmpSurface = _projectile->getSprite();

							if (itZ == 0)
							{
								// draw shadow on the floor
								Position voxelPos = _projectile->getPosition();
								voxelPos.z = 0;
								if (voxelPos.x / 16 >= mapPosition.x-1 &&
									voxelPos.y / 16 >= mapPosition.y-1 &&
									voxelPos.x / 16 <= mapPosition.x+1 &&
									voxelPos.y / 16 <= mapPosition.y+1 )
								{
									_camera->convertVoxelToScreen(voxelPos, &bulletPositionScreen);
									tmpSurface->blitNShade(surface, bulletPositionScreen.x - 16, bulletPositionScreen.y - 26, 15);
								}
							}

							Position voxelPos = _projectile->getPosition();
							if (voxelPos.x / 16 == mapPosition.x &&
								voxelPos.y / 16 == mapPosition.y )
							{
								_camera->convertVoxelToScreen(voxelPos, &bulletPositionScreen);
								tmpSurface->blitNShade(surface, bulletPositionScreen.x - 16, bulletPositionScreen.y - 26, 0);
							}
						}
						else
						{
							// draw bullet on the correct tile
							if (itX >= bulletLowX && itX <= bulletHighX && itY >= bulletLowY && itY <= bulletHighY)
							{
								if (itZ == 0)
								{
									// draw shadow on the floor
									for (int i = 1; i <= _projectile->getParticle(0); ++i)
									{
										if (_projectile->getParticle(i) != 0xFF)
										{
											Position voxelPos = _projectile->getPosition(1-i);
											voxelPos.z = 0;
											if (voxelPos.x / 16 == mapPosition.x &&
												voxelPos.y / 16 == mapPosition.y)
											{
												_camera->convertVoxelToScreen(voxelPos, &bulletPositionScreen);
												_bullet[_projectile->getParticle(i)]->blitNShade(surface, bulletPositionScreen.x, bulletPositionScreen.y, 15);
											}
										}
									}
								}
								for (int i = 1; i <= _projectile->getParticle(0); ++i)
								{
									if (_projectile->getParticle(i) != 0xFF)
									{
										Position voxelPos = _projectile->getPosition(1-i);
										if (voxelPos.x / 16 == mapPosition.x &&
											voxelPos.y / 16 == mapPosition.y )
										{
											_camera->convertVoxelToScreen(voxelPos, &bulletPositionScreen);
											_bullet[_projectile->getParticle(i)]->blitNShade(surface, bulletPositionScreen.x, bulletPositionScreen.y, 0);
										}
									}
								}
							}
						}
					}

					unit = tile->getUnit();
					// Draw soldier
					if (unit && (unit->getVisible() || _save->getDebugMode()))
					{
						tmpSurface = unit->getCache(&invalid);
						if (tmpSurface)
						{
							Position offset;
							calculateWalkingOffset(unit, &offset);
							tmpSurface->blitNShade(surface, screenPosition.x + offset.x, screenPosition.y + offset.y, tileShade);
							if (unit == (BattleUnit*)_save->getSelectedUnit() && _save->getSide() == FACTION_PLAYER)
							{
								_arrow->blitNShade(surface, screenPosition.x + offset.x + (_spriteWidth / 2) - (_arrow->getWidth() / 2), screenPosition.y + offset.y - _arrow->getHeight() + _animFrame, 0);
							}
							if (unit->getFire() > 0)
							{
								frameNumber = 4 + (_animFrame / 2);
								tmpSurface = _res->getSurfaceSet("SMOKE.PCK")->getFrame(frameNumber);
								tmpSurface->blitNShade(surface, screenPosition.x + offset.x, screenPosition.y + offset.y, 0);
							}
						}
					}
					// if we can see through the floor, draw the soldier below it if it is on stairs
					if (itZ > 0 && tile->hasNoFloor())
					{
						BattleUnit *tunit = _save->selectUnit(Position(itX, itY, itZ-1));
						Tile *ttile = _save->getTile(Position(itX, itY, itZ-1));
						if (tunit && ttile->getTerrainLevel() < 0 && ttile->isDiscovered(2))
						{
							tmpSurface = tunit->getCache(&invalid);
							if (tmpSurface)
							{
								Position offset;
								calculateWalkingOffset(tunit, &offset);
								offset.y += 24;
								tmpSurface->blitNShade(surface, screenPosition.x + offset.x, screenPosition.y + offset.y, tileShade);
								if (tunit == (BattleUnit*)_save->getSelectedUnit() && _save->getSide() == FACTION_PLAYER)
								{
									_arrow->blitNShade(surface, screenPosition.x + offset.x + (_spriteWidth / 2) - (_arrow->getWidth() / 2), screenPosition.y + offset.y - _arrow->getHeight() + _animFrame, 0);
								}
								if (tunit->getFire() > 0)
								{
									frameNumber = 4 + (_animFrame / 2);
									tmpSurface = _res->getSurfaceSet("SMOKE.PCK")->getFrame(frameNumber);
									tmpSurface->blitNShade(surface, screenPosition.x + offset.x, screenPosition.y + offset.y, 0);
								}
							}
						}
					}

					// Draw cursor front
					if (_selectorX == itX && _selectorY == itY && _cursorType != CT_NONE)
					{
						if (_camera->getViewHeight() == itZ)
						{
							if (_cursorType == CT_NORMAL || _cursorType == CT_THROW)
							{
								if (unit && (unit->getVisible() || _save->getDebugMode()))
									frameNumber = 4; // yellow box
								else
									frameNumber = 3; // red box
							}
							if (_cursorType == CT_AIM)
							{
								if (unit)
									frameNumber = 7 + (_animFrame / 2); // yellow animated crosshairs
								else
									frameNumber = 6; // red static crosshairs
							}
						}
						else if (_camera->getViewHeight() > itZ)
						{
							frameNumber = 5; // blue box
						}
						tmpSurface = _res->getSurfaceSet("CURSOR.PCK")->getFrame(frameNumber);
						tmpSurface->blitNShade(surface, screenPosition.x, screenPosition.y, 0);
						if (_cursorType == CT_THROW && _camera->getViewHeight() == itZ)
						{
							tmpSurface = _res->getSurfaceSet("CURSOR.PCK")->getFrame(15 + (_animFrame / 4));
							tmpSurface->blitNShade(surface, screenPosition.x, screenPosition.y, 0);
						}
					}

					// Draw smoke/fire
					if (tile->getFire() && tile->isDiscovered(2))
					{
						frameNumber = 0; // see http://www.ufopaedia.org/images/c/cb/Smoke.gif
						if ((_animFrame / 2) + tile->getAnimationOffset() > 3)
						{
							frameNumber += ((_animFrame / 2) + tile->getAnimationOffset() - 4);
						}
						else
						{
							frameNumber += (_animFrame / 2) + tile->getAnimationOffset();
						}
						tmpSurface = _res->getSurfaceSet("SMOKE.PCK")->getFrame(frameNumber);
						tmpSurface->blitNShade(surface, screenPosition.x, screenPosition.y, 0);
					}
					if (tile->getSmoke() && tile->isDiscovered(2))
					{
						frameNumber = 8 + int(floor((tile->getSmoke() / 5.0) - 0.1)); // see http://www.ufopaedia.org/images/c/cb/Smoke.gif
						if ((_animFrame / 2) + tile->getAnimationOffset() > 3)
						{
							frameNumber += ((_animFrame / 2) + tile->getAnimationOffset() - 4);
						}
						else
						{
							frameNumber += (_animFrame / 2) + tile->getAnimationOffset();
						}
						tmpSurface = _res->getSurfaceSet("SMOKE.PCK")->getFrame(frameNumber);
						tmpSurface->blitNShade(surface, screenPosition.x, screenPosition.y, 0);
					}
				}
			}
		}
	}

	// check if we got big explosions
	for (std::set<Explosion*>::const_iterator i = _explosions.begin(); i != _explosions.end(); ++i)
	{
		if ((*i)->isBig())
		{
			Position voxelPos = (*i)->getPosition();
			_camera->convertVoxelToScreen(voxelPos, &bulletPositionScreen);
			tmpSurface = _res->getSurfaceSet("X1.PCK")->getFrame((*i)->getCurrentFrame());
			tmpSurface->blitNShade(surface, bulletPositionScreen.x - 64, bulletPositionScreen.y - 64, 0);
			// if the projectile is outside the viewport - center it back on it
			if (bulletPositionScreen.x < -_spriteWidth || bulletPositionScreen.x > surface->getWidth() ||
				bulletPositionScreen.y < -_spriteHeight || bulletPositionScreen.y > surface->getHeight()  )
			{
				_camera->centerOnPosition(Position(voxelPos.x/16, voxelPos.y/16, voxelPos.z/24));
			}
		}
		else
		{
			Position voxelPos = (*i)->getPosition();
			_camera->convertVoxelToScreen(voxelPos, &bulletPositionScreen);
			tmpSurface = _res->getSurfaceSet("SMOKE.PCK")->getFrame((*i)->getCurrentFrame());
			tmpSurface->blitNShade(surface, bulletPositionScreen.x - 15, bulletPositionScreen.y - 15, 0);
		}
	}

	surface->unlock();
}

/**
 * Handles map mouse shortcuts.
 * @param action Pointer to an action.
 * @param state State that the action handlers belong to.
 */
void Map::mouseClick(Action *action, State *state)
{
	InteractiveSurface::mouseClick(action, state);
	_camera->mouseClick(action, state);
}

/**
 * Handles map keyboard shortcuts.
 * @param action Pointer to an action.
 * @param state State that the action handlers belong to.
 */
void Map::keyboardPress(Action *action, State *state)
{
	InteractiveSurface::keyboardPress(action, state);
}

/**
 * Handles mouse over events.
 * @param action Pointer to an action.
 * @param state State that the action handlers belong to.
 */
void Map::mouseOver(Action *action, State *state)
{
	int posX = action->getXMouse();
	int posY = action->getYMouse();

	_camera->mouseOver(action, state);

	setSelectorPosition((int)((double)posX / action->getXScale()), (int)((double)posY / action->getYScale()));
}


/**
 * Sets the selector to a certain tile on the map.
 * @param mx mouse x position
 * @param my mouse y position
 */
void Map::setSelectorPosition(int mx, int my)
{
	int oldX = _selectorX, oldY = _selectorY;

	if (!mx && !my) return; // cursor is offscreen
	_camera->convertScreenToMap(mx, my, &_selectorX, &_selectorY);

	if (oldX != _selectorX || oldY != _selectorY)
	{
		_redraw = true;
	}
}

/**
 * Handle animating tiles. 8 Frames per animation.
 * @param redraw Redraw the battlescape?
 */
void Map::animate(bool redraw)
{
	_animFrame++;
	if (_animFrame == 8) _animFrame = 0;

	for (int i = 0; i < _save->getWidth()*_save->getHeight()*_save->getLength(); ++i)
	{
		_save->getTiles()[i]->animate();
	}
	if (redraw) _redraw = true;
}

/**
 * Draws the rectangle selector.
 * @param pos pointer to a position
 */
void Map::getSelectorPosition(Position *pos) const
{
	pos->x = _selectorX;
	pos->y = _selectorY;
	pos->z = _camera->getViewHeight();
}

/**
 * Calculate the offset of a soldier, when it is walking in the middle of 2 tiles.
 * @param unit pointer to BattleUnit
 * @param offset pointer to the offset to return the calculation.
 */
void Map::calculateWalkingOffset(BattleUnit *unit, Position *offset)
{
	int offsetX[8] = { 1, 1, 1, 0, -1, -1, -1, 0 };
	int offsetY[8] = { 1, 0, -1, -1, -1, 0, 1, 1 };
	int phase = unit->getWalkingPhase() + unit->getDiagonalWalkingPhase();
	int dir = unit->getDirection();
	int midphase = 4 + 4 * (dir % 2);
	int endphase = 8 + 8 * (dir % 2);

	if (unit->getVerticalDirection())
	{
		midphase = 4;
		endphase = 8;
	}
	else
	if ((unit->getStatus() == STATUS_WALKING || unit->getStatus() == STATUS_FLYING))
	{
		if (phase < midphase)
		{
			offset->x = phase * 2 * offsetX[dir];
			offset->y = - phase * offsetY[dir];
		}
		else
		{
			offset->x = (phase - endphase) * 2 * offsetX[dir];
			offset->y = - (phase - endphase) * offsetY[dir];
		}
	}

	// If we are walking in between tiles, interpolate it's terrain level.
	if (unit->getStatus() == STATUS_WALKING || unit->getStatus() == STATUS_FLYING)
	{
		if (phase < midphase)
		{
			int fromLevel = unit->getTile()->getTerrainLevel();
			int toLevel = _save->getTile(unit->getDestination())->getTerrainLevel();
			if (unit->getPosition().z > unit->getDestination().z)
			{
				// going down a level, so toLevel 0 becomes +24, -8 becomes  16
				toLevel += 24*(unit->getPosition().z - unit->getDestination().z);
			}else if (unit->getPosition().z < unit->getDestination().z)
			{
				// going up a level, so toLevel 0 becomes -24, -8 becomes -16
				toLevel = -24*(unit->getDestination().z - unit->getPosition().z) + abs(toLevel);
			}
			offset->y += ((fromLevel * (endphase - phase)) / endphase) + ((toLevel * (phase)) / endphase);
		}
		else
		{
			// from phase 4 onwards the unit behind the scenes already is on the destination tile
			// we have to get it's last position to calculate the correct offset
			int fromLevel = _save->getTile(unit->getLastPosition())->getTerrainLevel();
			int toLevel = _save->getTile(unit->getDestination())->getTerrainLevel();
			if (unit->getLastPosition().z > unit->getDestination().z)
			{
				// going down a level, so fromLevel 0 becomes -24, -8 becomes -32
				fromLevel -= 24*(unit->getLastPosition().z - unit->getDestination().z);
			}else if (unit->getLastPosition().z < unit->getDestination().z)
			{
				// going up a level, so fromLevel 0 becomes +24, -8 becomes 16
				fromLevel = 24*(unit->getDestination().z - unit->getLastPosition().z) - abs(fromLevel);
			}
			offset->y += ((fromLevel * (endphase - phase)) / endphase) + ((toLevel * (phase)) / endphase);
		}
	}
	else
	{
		offset->y += unit->getTile()->getTerrainLevel();
	}

}

/**
 * Set the 3D cursor to selection/aim mode
 * @param type
 */
void Map::setCursorType(CursorType type)
{
	_cursorType = type;
}

/**
 * Get cursor type.
 * @return cursortype
 */
CursorType Map::getCursorType() const
{
	return _cursorType;
}


/**
 * Check all units if they need to be redrawn.
 */
void Map::cacheUnits()
{
	for (std::vector<BattleUnit*>::iterator i = _save->getUnits()->begin(); i != _save->getUnits()->end(); ++i)
	{
		cacheUnit(*i);
	}
}

/**
 * Check if a certain unit needs to be redrawn.
 * @param unit Pointer to battleUnit
 */
void Map::cacheUnit(BattleUnit *unit)
{
	UnitSprite *unitSprite = new UnitSprite(_spriteWidth, _spriteHeight, 0, 0);
	unitSprite->setPalette(this->getPalette());
	bool invalid;

	Surface *cache = unit->getCache(&invalid);
	if (invalid)
	{
		if (!cache) // no cache created yet
		{
			cache = new Surface(_spriteWidth, _spriteHeight);
			cache->setPalette(this->getPalette());
		}
		unitSprite->setBattleUnit(unit);
		BattleItem *handItem = unit->getItem("STR_RIGHT_HAND");
		if (handItem)
		{
			unitSprite->setBattleItem(handItem);
		}
		else
		{
			unitSprite->setBattleItem(0);
		}
		unitSprite->setSurfaces(_res->getSurfaceSet(unit->getUnit()->getArmor()->getSpriteSheet()),
								_res->getSurfaceSet("HANDOB.PCK"));

		cache->clear();
		unitSprite->blit(cache);
		unit->setCache(cache);
	}
	delete unitSprite;
}

/**
 * Put a projectile sprite on the map
 * @param projectile
 */
void Map::setProjectile(Projectile *projectile)
{
	_projectile = projectile;
}

/**
 * Get the current projectile sprite on the map
 * @return 0 if there is no projectile sprite on the map.
 */
Projectile *Map::getProjectile() const
{
	return _projectile;
}

/**
 * Get a list of explosion sprites on the map.
 * @return a list of explosion sprites.
 */
std::set<Explosion*> *Map::getExplosions()
{
	return &_explosions;
}

/**
 * Get pointer to camera
 * @return pointer to camera
*/
Camera *Map::getCamera()
{
	return _camera;
}

/**
 * Timers only work on surfaces so we have to pass this on to the camera object.
*/
void Map::scroll()
{
	_camera->scroll();
}

}

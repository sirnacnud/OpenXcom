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
#include "BattleSoldier.h"

namespace OpenXcom
{

/**
 * Initializes a BattleSoldier.
 * @param soldier pointer to the geoscape soldier object
 * @param renderRules pointer to RuleUnitSprite
 */
BattleSoldier::BattleSoldier(Soldier *soldier, RuleUnitSprite *renderRules): BattleUnit(renderRules), _soldier(soldier)
{
}

/**
 *
 */
BattleSoldier::~BattleSoldier()
{
}

/**
 * gets the underlying soldier object
 * @return pointer to soldier
 */
Soldier *BattleSoldier::getSoldier()
{
	return _soldier;
}

}
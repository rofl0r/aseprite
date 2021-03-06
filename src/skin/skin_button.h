/* ASEPRITE
 * Copyright (C) 2001-2013  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef SKIN_BUTTON_H_INCLUDED
#define SKIN_BUTTON_H_INCLUDED

#include "skin/skin_parts.h"
#include "skin/skin_theme.h"
#include "ui/button.h"
#include "ui/graphics.h"
#include "ui/paint_event.h"

template<typename Base = ui::Button>
class SkinButton : public Base
{
public:
  SkinButton(SkinPart partNormal,
             SkinPart partHot,
             SkinPart partSelected)
    : Base("")
    , m_partNormal(partNormal)
    , m_partHot(partHot)
    , m_partSelected(partSelected)
  {
  }

  void setParts(SkinPart partNormal,
                SkinPart partHot,
                SkinPart partSelected) {
    m_partNormal = partNormal;
    m_partHot = partHot;
    m_partSelected = partSelected;
    Base::invalidate();
  }

protected:
  void onPaint(ui::PaintEvent& ev) OVERRIDE {
    gfx::Rect bounds(Base::getClientBounds());
    ui::Graphics* g = ev.getGraphics();
    SkinTheme* theme = static_cast<SkinTheme*>(Base::getTheme());
    SkinPart part;

    if (Base::isSelected())
      part = m_partSelected;
    else if (Base::hasMouseOver())
      part = m_partHot;
    else
      part = m_partNormal;

    g->drawAlphaBitmap(theme->get_part(part), bounds.x, bounds.y);
  }

private:
  SkinPart m_partNormal;
  SkinPart m_partHot;
  SkinPart m_partSelected;
};

#endif

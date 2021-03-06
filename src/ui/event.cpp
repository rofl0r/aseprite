// ASEPRITE gui library
// Copyright (C) 2001-2013  David Capello
//
// This source file is distributed under a BSD-like license, please
// read LICENSE.txt for more information.

#include "ui/event.h"

namespace ui {

Event::Event(Component* source)
  : m_source(source)
{
}

Event::~Event()
{
}

Component* Event::getSource()
{
  return m_source;
}

} // namespace ui

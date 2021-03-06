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

#ifndef UI_CONTEXT_H_INCLUDED
#define UI_CONTEXT_H_INCLUDED

#include "base/compiler_specific.h"
#include "context.h"

class Editor;
namespace widgets { class DocumentView; }

typedef std::vector<widgets::DocumentView*> DocumentViews;

class UIContext : public Context
{
public:
  static UIContext* instance() { return m_instance; }

  UIContext();
  virtual ~UIContext();

  virtual bool isUiAvailable() const { return true; }

  widgets::DocumentView* getActiveView() const;
  void setActiveView(widgets::DocumentView* documentView);

  // Returns the number of views that the given document has.
  size_t countViewsOf(Document* document) const;

  // Returns the current editor. It can be null.
  Editor* getActiveEditor();

  // Returns the active editor for the given document, or creates a
  // new one if it's necessary.
  Editor* getEditorFor(Document* document);

protected:
  virtual void onAddDocument(Document* document) OVERRIDE;
  virtual void onRemoveDocument(Document* document) OVERRIDE;
  virtual void onGetActiveLocation(DocumentLocation* location) const OVERRIDE;

private:
  static UIContext* m_instance;
};

#endif

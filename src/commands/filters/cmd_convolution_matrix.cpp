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

#include "config.h"

#include "app/color.h"
#include "app/find_widget.h"
#include "app/load_widget.h"
#include "base/bind.h"
#include "commands/command.h"
#include "commands/filters/convolution_matrix_stock.h"
#include "commands/filters/filter_manager_impl.h"
#include "commands/filters/filter_window.h"
#include "context.h"
#include "filters/convolution_matrix.h"
#include "filters/convolution_matrix_filter.h"
#include "ini_file.h"
#include "raster/mask.h"
#include "raster/sprite.h"
#include "settings/document_settings.h"
#include "ui/button.h"
#include "ui/label.h"
#include "ui/listbox.h"
#include "ui/slider.h"
#include "ui/view.h"
#include "ui/widget.h"
#include "ui/window.h"

#include <string.h>

static const char* ConfigSection = "ConvolutionMatrix";

using namespace ui;

//////////////////////////////////////////////////////////////////////
// Convolution Matrix window

class ConvolutionMatrixWindow : public FilterWindow
{
public:
  ConvolutionMatrixWindow(ConvolutionMatrixFilter& filter, FilterManagerImpl& filterMgr, ConvolutionMatrixStock& stock)
    : FilterWindow("Convolution Matrix", ConfigSection, &filterMgr,
                   WithChannelsSelector,
                   WithTiledCheckBox,
                   filter.getTiledMode())
    , m_filter(filter)
    , m_controlsWidget(app::load_widget<Widget>("convolution_matrix.xml", "controls"))
    , m_stock(stock)
    , m_view(app::find_widget<View>(m_controlsWidget, "view"))
    , m_stockListBox(app::find_widget<ListBox>(m_controlsWidget, "stock"))
    , m_reloadButton(app::find_widget<Button>(m_controlsWidget, "reload"))
  {
    getContainer()->addChild(m_controlsWidget);

    m_reloadButton->Click.connect(&ConvolutionMatrixWindow::onReloadStock, this);
    m_stockListBox->ChangeSelectedItem.connect(Bind<void>(&ConvolutionMatrixWindow::onMatrixChange, this));

    fillStockListBox();
  }

private:
  void onReloadStock(Event& ev)
  {
    m_stock.reloadStock();
    fillStockListBox();
  }

  void setupTiledMode(TiledMode tiledMode)
  {
    m_filter.setTiledMode(tiledMode);
  }

  void fillStockListBox()
  {
    const char* oldSelected = (m_filter.getMatrix() ? m_filter.getMatrix()->getName(): NULL);

    // Clean the list
    while (!m_stockListBox->getChildren().empty()) {
      Widget* listitem = m_stockListBox->getChildren().front();
      m_stockListBox->removeChild(listitem);
      delete listitem;
    }

    for (ConvolutionMatrixStock::iterator it = m_stock.begin(), end = m_stock.end();
         it != end; ++it) {
      SharedPtr<ConvolutionMatrix> matrix = *it;
      ListBox::Item* listitem = new ListBox::Item(matrix->getName());
      m_stockListBox->addChild(listitem);
    }

    selectMatrixByName(oldSelected);
  }

  void selectMatrixByName(const char* oldSelected)
  {
    Widget* select_this = UI_FIRST_WIDGET(m_stockListBox->getChildren());

    if (oldSelected) {
      UI_FOREACH_WIDGET(m_stockListBox->getChildren(), it) {
        Widget* child = *it;

        if (strcmp(child->getText(), oldSelected) == 0) {
          select_this = child;
          break;
        }
      }
    }

    if (select_this) {
      select_this->setSelected(true);
      onMatrixChange();
    }

    m_view->updateView();
  }

  void onMatrixChange()
  {
    ListBox::Item* selected = m_stockListBox->getSelectedChild();
    SharedPtr<ConvolutionMatrix> matrix = m_stock.getByName(selected->getText());
    Target newTarget = matrix->getDefaultTarget();

    m_filter.setMatrix(matrix);

    setNewTarget(newTarget);

    restartPreview();
  }

  ConvolutionMatrixFilter& m_filter;
  UniquePtr<Widget> m_controlsWidget;
  ConvolutionMatrixStock& m_stock;
  View* m_view;
  ListBox* m_stockListBox;
  Button* m_reloadButton;
};

//////////////////////////////////////////////////////////////////////
// Convolution Matrix command

class ConvolutionMatrixCommand : public Command
{
public:
  ConvolutionMatrixCommand();
  Command* clone() const { return new ConvolutionMatrixCommand(*this); }

protected:
  bool onEnabled(Context* context);
  void onExecute(Context* context);
};

ConvolutionMatrixCommand::ConvolutionMatrixCommand()
  : Command("ConvolutionMatrix",
            "Convolution Matrix",
            CmdRecordableFlag)
{
}

bool ConvolutionMatrixCommand::onEnabled(Context* context)
{
  return context->checkFlags(ContextFlags::ActiveDocumentIsWritable |
                             ContextFlags::HasActiveSprite);
}

void ConvolutionMatrixCommand::onExecute(Context* context)
{
  // Load stock
  ConvolutionMatrixStock m_stock;

  // Get last used (selected) matrix
  SharedPtr<ConvolutionMatrix> matrix =
    m_stock.getByName(get_config_string(ConfigSection, "Selected", ""));

  // Create the filter and setup initial settings
  ConvolutionMatrixFilter filter;
  IDocumentSettings* docSettings = context->getSettings()->getDocumentSettings(context->getActiveDocument());
  filter.setTiledMode(docSettings->getTiledMode());
  if (matrix != 0)
    filter.setMatrix(matrix);

  FilterManagerImpl filterMgr(context, &filter);

  ConvolutionMatrixWindow window(filter, filterMgr, m_stock);
  if (window.doModal()) {
    if (filter.getMatrix() != NULL)
      set_config_string(ConfigSection, "Selected", filter.getMatrix()->getName());
  }
}

//////////////////////////////////////////////////////////////////////
// CommandFactory

Command* CommandFactory::createConvolutionMatrixCommand()
{
  return new ConvolutionMatrixCommand;
}

// ASEPRITE gui library
// Copyright (C) 2001-2013  David Capello
//
// This source file is distributed under a BSD-like license, please
// read LICENSE.txt for more information.

#ifndef UI_ALERT_H_INCLUDED
#define UI_ALERT_H_INCLUDED

#include "base/shared_ptr.h"
#include "ui/window.h"

namespace ui {

  class Alert;
  typedef SharedPtr<Alert> AlertPtr;

  class Alert : public Window
  {
  public:
    Alert();

    static AlertPtr create(const char* format, ...);
    static int show(const char* format, ...);

  private:
    void processString(char* buf, std::vector<Widget*>& labels, std::vector<Widget*>& buttons);
  };

} // namespace ui

#endif

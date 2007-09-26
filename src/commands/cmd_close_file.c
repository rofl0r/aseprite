/* ase -- allegro-sprite-editor: the ultimate sprites factory
 * Copyright (C) 2001-2005, 2007  David A. Capello
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

#ifndef USE_PRECOMPILED_HEADER

#include <allegro.h>
#include "jinete.h"

#include "commands/commands.h"
#include "core/app.h"
#include "modules/sprites.h"
#include "raster/sprite.h"

#endif

bool command_enabled_close_file(const char *argument)
{
  return current_sprite != NULL;
}

void command_execute_close_file(const char *argument)
{
  Sprite *sprite = current_sprite;

  /* see if the sprite has changes */
  while (sprite_is_modified(sprite)) {
    /* ask what want to do the user with the changes in the sprite */
    int ret = jalert("%s<<%s<<%s||%s",
		     _("Warning"),
		     _("Saving changes in:"),
		     get_filename(sprite->filename),
		     _("&Save||&Discard||&Cancel"));

    if (ret == 1) {
      /* "save": save the changes */
      command_execute(command_get_by_name(CMD_SAVE_FILE), NULL);
    }
    else if (ret != 2) {
      /* "cancel" or "ESC" */
      return; /* we back doing nothing */
    }
    else {
      /* "discard" */
      break;
    }
  }

  /* closes the sprite */
  sprite_unmount(sprite);
  sprite_free(sprite);
}
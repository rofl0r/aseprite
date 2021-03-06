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

#include "base/exception.h"
#include "document.h"
#include "file/file.h"
#include "file/file_format.h"
#include "file/file_handle.h"
#include "file/format_options.h"
#include "raster/raster.h"
#include "zlib.h"

#include <stdio.h>

#define ASE_FILE_MAGIC                  0xA5E0
#define ASE_FILE_FRAME_MAGIC            0xF1FA

#define ASE_FILE_CHUNK_FLI_COLOR2       4
#define ASE_FILE_CHUNK_FLI_COLOR        11
#define ASE_FILE_CHUNK_LAYER            0x2004
#define ASE_FILE_CHUNK_CEL              0x2005
#define ASE_FILE_CHUNK_MASK             0x2016
#define ASE_FILE_CHUNK_PATH             0x2017

#define ASE_FILE_RAW_CEL                0
#define ASE_FILE_LINK_CEL               1
#define ASE_FILE_COMPRESSED_CEL         2

typedef struct ASE_Header
{
  long pos;

  uint32_t size;
  uint16_t magic;
  uint16_t frames;
  uint16_t width;
  uint16_t height;
  uint16_t depth;
  uint32_t flags;
  uint16_t speed;       // Deprecated, use "duration" of FrameHeader
  uint32_t next;
  uint32_t frit;
  uint8_t transparent_index;
  uint8_t ignore[3];
  uint16_t ncolors;
} ASE_Header;

typedef struct ASE_FrameHeader
{
  uint32_t size;
  uint16_t magic;
  uint16_t chunks;
  uint16_t duration;
} ASE_FrameHeader;

// TODO Warning: the writing routines aren't thread-safe
static ASE_FrameHeader *current_frame_header = NULL;
static int chunk_type;
static int chunk_start;

static bool ase_file_read_header(FILE* f, ASE_Header* header);
static void ase_file_prepare_header(FILE* f, ASE_Header* header, const Sprite* sprite);
static void ase_file_write_header(FILE* f, ASE_Header* header);

static void ase_file_read_frame_header(FILE *f, ASE_FrameHeader *frame_header);
static void ase_file_prepare_frame_header(FILE *f, ASE_FrameHeader *frame_header);
static void ase_file_write_frame_header(FILE *f, ASE_FrameHeader *frame_header);

static void ase_file_write_layers(FILE *f, Layer *layer);
static void ase_file_write_cels(FILE *f, Sprite *sprite, Layer *layer, FrameNumber frame);

static void ase_file_read_padding(FILE *f, int bytes);
static void ase_file_write_padding(FILE *f, int bytes);
static std::string ase_file_read_string(FILE *f);
static void ase_file_write_string(FILE *f, const std::string& string);

static void ase_file_write_start_chunk(FILE *f, int type);
static void ase_file_write_close_chunk(FILE *f);

static Palette *ase_file_read_color_chunk(FILE *f, Sprite *sprite, FrameNumber frame);
static Palette *ase_file_read_color2_chunk(FILE *f, Sprite *sprite, FrameNumber frame);
static void ase_file_write_color2_chunk(FILE *f, Palette *pal);
static Layer *ase_file_read_layer_chunk(FILE *f, Sprite *sprite, Layer **previous_layer, int *current_level);
static void ase_file_write_layer_chunk(FILE *f, Layer *layer);
static Cel *ase_file_read_cel_chunk(FILE *f, Sprite *sprite, FrameNumber frame, PixelFormat pixelFormat, FileOp *fop, ASE_Header *header, size_t chunk_end);
static void ase_file_write_cel_chunk(FILE *f, Cel *cel, LayerImage *layer, Sprite *sprite);
static Mask *ase_file_read_mask_chunk(FILE *f);
static void ase_file_write_mask_chunk(FILE *f, Mask *mask);

class AseFormat : public FileFormat
{
  const char* onGetName() const { return "ase"; }
  const char* onGetExtensions() const { return "ase,aseprite"; }
  int onGetFlags() const {
    return
      FILE_SUPPORT_LOAD |
      FILE_SUPPORT_SAVE |
      FILE_SUPPORT_RGB |
      FILE_SUPPORT_RGBA |
      FILE_SUPPORT_GRAY |
      FILE_SUPPORT_GRAYA |
      FILE_SUPPORT_INDEXED |
      FILE_SUPPORT_LAYERS |
      FILE_SUPPORT_FRAMES |
      FILE_SUPPORT_PALETTES;
  }

  bool onLoad(FileOp* fop);
  bool onSave(FileOp* fop);
};

FileFormat* CreateAseFormat()
{
  return new AseFormat;
}

bool AseFormat::onLoad(FileOp *fop)
{
  FileHandle f(fop->filename.c_str(), "rb");
  if (!f)
    return false;

  ASE_Header header;
  if (!ase_file_read_header(f, &header)) {
    fop_error(fop, "Error reading header\n");
    return false;
  }

  // Create the new sprite
  Sprite *sprite = new Sprite(header.depth == 32 ? IMAGE_RGB:
                              header.depth == 16 ? IMAGE_GRAYSCALE: IMAGE_INDEXED,
                              header.width, header.height, header.ncolors);
  if (!sprite) {
    fop_error(fop, "Error creating sprite with file spec\n");
    return false;
  }

  // Set frames and speed
  sprite->setTotalFrames(FrameNumber(header.frames));
  sprite->setDurationForAllFrames(header.speed);

  // Set transparent entry
  sprite->setTransparentColor(header.transparent_index);

  // Prepare variables for layer chunks
  Layer* last_layer = sprite->getFolder();
  int current_level = -1;

  /* read frame by frame to end-of-file */
  for (FrameNumber frame(0); frame<sprite->getTotalFrames(); ++frame) {
    /* start frame position */
    int frame_pos = ftell(f);
    fop_progress(fop, (float)frame_pos / (float)header.size);

    /* read frame header */
    ASE_FrameHeader frame_header;
    ase_file_read_frame_header(f, &frame_header);

    /* correct frame type */
    if (frame_header.magic == ASE_FILE_FRAME_MAGIC) {
      /* use frame-duration field? */
      if (frame_header.duration > 0)
        sprite->setFrameDuration(frame, frame_header.duration);

      /* read chunks */
      for (int c=0; c<frame_header.chunks; c++) {
        /* start chunk position */
        int chunk_pos = ftell(f);
        fop_progress(fop, (float)chunk_pos / (float)header.size);

        /* read chunk information */
        int chunk_size = fgetl(f);
        int chunk_type = fgetw(f);

        switch (chunk_type) {

          /* only for 8 bpp images */
          case ASE_FILE_CHUNK_FLI_COLOR:
          case ASE_FILE_CHUNK_FLI_COLOR2:
            /* fop_error(fop, "Color chunk\n"); */

            if (sprite->getPixelFormat() == IMAGE_INDEXED) {
              Palette* prev_pal = sprite->getPalette(frame);
              Palette* pal =
                chunk_type == ASE_FILE_CHUNK_FLI_COLOR ?
                ase_file_read_color_chunk(f, sprite, frame):
                ase_file_read_color2_chunk(f, sprite, frame);

              if (prev_pal->countDiff(pal, NULL, NULL) > 0) {
                sprite->setPalette(pal, true);
              }

              delete pal;
            }
            else
              fop_error(fop, "Warning: was found a color chunk in non-8bpp file\n");
            break;

          case ASE_FILE_CHUNK_LAYER: {
            /* fop_error(fop, "Layer chunk\n"); */

            ase_file_read_layer_chunk(f, sprite,
                                      &last_layer,
                                      &current_level);
            break;
          }

          case ASE_FILE_CHUNK_CEL: {
            /* fop_error(fop, "Cel chunk\n"); */

            ase_file_read_cel_chunk(f, sprite, frame,
                                    sprite->getPixelFormat(), fop, &header,
                                    chunk_pos+chunk_size);
            break;
          }

          case ASE_FILE_CHUNK_MASK: {
            Mask *mask;

            /* fop_error(fop, "Mask chunk\n"); */

            mask = ase_file_read_mask_chunk(f);
            if (mask)
              delete mask;      // TODO add the mask in some place?
            else
              fop_error(fop, "Warning: error loading a mask chunk\n");

            break;
          }

          case ASE_FILE_CHUNK_PATH:
            /* fop_error(fop, "Path chunk\n"); */
            break;

          default:
            fop_error(fop, "Warning: Unsupported chunk type %d (skipping)\n", chunk_type);
            break;
        }

        /* skip chunk size */
        fseek(f, chunk_pos+chunk_size, SEEK_SET);
      }
    }

    /* skip frame size */
    fseek(f, frame_pos+frame_header.size, SEEK_SET);

    /* just one frame? */
    if (fop->oneframe)
      break;

    if (fop_is_stop(fop))
      break;
  }

  fop->document = new Document(sprite);

  if (ferror(f)) {
    fop_error(fop, "Error reading file.\n");
    return false;
  }
  else {
    return true;
  }
}

bool AseFormat::onSave(FileOp *fop)
{
  Sprite* sprite = fop->document->getSprite();
  ASE_Header header;
  ASE_FrameHeader frame_header;

  FileHandle f(fop->filename.c_str(), "wb");

  /* prepare the header */
  ase_file_prepare_header(f, &header, sprite);

  /* write frame */
  for (FrameNumber frame(0); frame<sprite->getTotalFrames(); ++frame) {
    /* prepare the header */
    ase_file_prepare_frame_header(f, &frame_header);

    /* frame duration */
    frame_header.duration = sprite->getFrameDuration(frame);

    /* the sprite is indexed and the palette changes? (or is the first frame) */
    if (sprite->getPixelFormat() == IMAGE_INDEXED &&
        (frame == 0 ||
         sprite->getPalette(frame.previous())->countDiff(sprite->getPalette(frame), NULL, NULL) > 0)) {
      /* write the color chunk */
      ase_file_write_color2_chunk(f, sprite->getPalette(frame));
    }

    /* write extra chunks in the first frame */
    if (frame == 0) {
      LayerIterator it = sprite->getFolder()->getLayerBegin();
      LayerIterator end = sprite->getFolder()->getLayerEnd();

      /* write layer chunks */
      for (; it != end; ++it)
        ase_file_write_layers(f, *it);
    }

    /* write cel chunks */
    ase_file_write_cels(f, sprite, sprite->getFolder(), frame);

    /* write the frame header */
    ase_file_write_frame_header(f, &frame_header);

    /* progress */
    if (sprite->getTotalFrames() > 1)
      fop_progress(fop, (float)(frame.next()) / (float)(sprite->getTotalFrames()));
  }

  /* write the header */
  ase_file_write_header(f, &header);

  if (ferror(f)) {
    fop_error(fop, "Error writing file.\n");
    return false;
  }
  else {
    return true;
  }
}

static bool ase_file_read_header(FILE *f, ASE_Header *header)
{
  header->pos = ftell(f);

  header->size  = fgetl(f);
  header->magic = fgetw(f);
  if (header->magic != ASE_FILE_MAGIC)
    return false;

  header->frames     = fgetw(f);
  header->width      = fgetw(f);
  header->height     = fgetw(f);
  header->depth      = fgetw(f);
  header->flags      = fgetl(f);
  header->speed      = fgetw(f);
  header->next       = fgetl(f);
  header->frit       = fgetl(f);
  header->transparent_index = fgetc(f);
  header->ignore[0]  = fgetc(f);
  header->ignore[1]  = fgetc(f);
  header->ignore[2]  = fgetc(f);
  header->ncolors    = fgetw(f);
  if (header->ncolors == 0)     // 0 means 256 (old .ase files)
    header->ncolors = 256;

  fseek(f, header->pos+128, SEEK_SET);
  return true;
}

static void ase_file_prepare_header(FILE *f, ASE_Header *header, const Sprite* sprite)
{
  header->pos = ftell(f);

  header->size = 0;
  header->magic = ASE_FILE_MAGIC;
  header->frames = sprite->getTotalFrames();
  header->width = sprite->getWidth();
  header->height = sprite->getHeight();
  header->depth = (sprite->getPixelFormat() == IMAGE_RGB ? 32:
                   sprite->getPixelFormat() == IMAGE_GRAYSCALE ? 16:
                   sprite->getPixelFormat() == IMAGE_INDEXED ? 8: 0);
  header->flags = 0;
  header->speed = sprite->getFrameDuration(FrameNumber(0));
  header->next = 0;
  header->frit = 0;
  header->transparent_index = sprite->getTransparentColor();
  header->ignore[0] = 0;
  header->ignore[1] = 0;
  header->ignore[2] = 0;
  header->ncolors = sprite->getPalette(FrameNumber(0))->size();

  fseek(f, header->pos+128, SEEK_SET);
}

static void ase_file_write_header(FILE *f, ASE_Header *header)
{
  header->size = ftell(f)-header->pos;

  fseek(f, header->pos, SEEK_SET);

  fputl(header->size, f);
  fputw(header->magic, f);
  fputw(header->frames, f);
  fputw(header->width, f);
  fputw(header->height, f);
  fputw(header->depth, f);
  fputl(header->flags, f);
  fputw(header->speed, f);
  fputl(header->next, f);
  fputl(header->frit, f);
  fputc(header->transparent_index, f);
  fputc(header->ignore[0], f);
  fputc(header->ignore[1], f);
  fputc(header->ignore[2], f);
  fputw(header->ncolors, f);

  fseek(f, header->pos+header->size, SEEK_SET);
}

static void ase_file_read_frame_header(FILE *f, ASE_FrameHeader *frame_header)
{
  frame_header->size = fgetl(f);
  frame_header->magic = fgetw(f);
  frame_header->chunks = fgetw(f);
  frame_header->duration = fgetw(f);
  ase_file_read_padding(f, 6);
}

static void ase_file_prepare_frame_header(FILE *f, ASE_FrameHeader *frame_header)
{
  int pos = ftell(f);

  frame_header->size = pos;
  frame_header->magic = ASE_FILE_FRAME_MAGIC;
  frame_header->chunks = 0;
  frame_header->duration = 0;

  current_frame_header = frame_header;

  fseek(f, pos+16, SEEK_SET);
}

static void ase_file_write_frame_header(FILE *f, ASE_FrameHeader *frame_header)
{
  int pos = frame_header->size;
  int end = ftell(f);

  frame_header->size = end-pos;

  fseek(f, pos, SEEK_SET);

  fputl(frame_header->size, f);
  fputw(frame_header->magic, f);
  fputw(frame_header->chunks, f);
  fputw(frame_header->duration, f);
  ase_file_write_padding(f, 6);

  fseek(f, end, SEEK_SET);

  current_frame_header = NULL;
}

static void ase_file_write_layers(FILE *f, Layer *layer)
{
  ase_file_write_layer_chunk(f, layer);

  if (layer->isFolder()) {
    LayerIterator it = static_cast<LayerFolder*>(layer)->getLayerBegin();
    LayerIterator end = static_cast<LayerFolder*>(layer)->getLayerEnd();

    for (; it != end; ++it)
      ase_file_write_layers(f, *it);
  }
}

static void ase_file_write_cels(FILE *f, Sprite *sprite, Layer *layer, FrameNumber frame)
{
  if (layer->isImage()) {
    Cel* cel = static_cast<LayerImage*>(layer)->getCel(frame);
    if (cel) {
/*       fop_error(fop, "New cel in frame %d, in layer %d\n", */
/*                   frame, sprite_layer2index(sprite, layer)); */

      ase_file_write_cel_chunk(f, cel, static_cast<LayerImage*>(layer), sprite);
    }
  }

  if (layer->isFolder()) {
    LayerIterator it = static_cast<LayerFolder*>(layer)->getLayerBegin();
    LayerIterator end = static_cast<LayerFolder*>(layer)->getLayerEnd();

    for (; it != end; ++it)
      ase_file_write_cels(f, sprite, *it, frame);
  }
}

static void ase_file_read_padding(FILE *f, int bytes)
{
  for (int c=0; c<bytes; c++)
    fgetc(f);
}

static void ase_file_write_padding(FILE *f, int bytes)
{
  for (int c=0; c<bytes; c++)
    fputc(0, f);
}

static std::string ase_file_read_string(FILE *f)
{
  int length = fgetw(f);
  if (length == EOF)
    return "";

  std::string string;
  string.reserve(length+1);

  for (int c=0; c<length; c++)
    string.push_back(fgetc(f));

  return string;
}

static void ase_file_write_string(FILE *f, const std::string& string)
{
  fputw(string.size(), f);

  for (size_t c=0; c<string.size(); ++c)
    fputc(string[c], f);
}

static void ase_file_write_start_chunk(FILE *f, int type)
{
  current_frame_header->chunks++;

  chunk_type = type;
  chunk_start = ftell(f);

  fseek(f, chunk_start+6, SEEK_SET);
}

static void ase_file_write_close_chunk(FILE *f)
{
  int chunk_end = ftell(f);
  int chunk_size = chunk_end - chunk_start;

  fseek(f, chunk_start, SEEK_SET);
  fputl(chunk_size, f);
  fputw(chunk_type, f);
  fseek(f, chunk_end, SEEK_SET);
}

static Palette *ase_file_read_color_chunk(FILE *f, Sprite *sprite, FrameNumber frame)
{
  int i, c, r, g, b, packets, skip, size;
  Palette* pal = new Palette(*sprite->getPalette(frame));
  pal->setFrame(frame);

  packets = fgetw(f);   // Number of packets
  skip = 0;

  // Read all packets
  for (i=0; i<packets; i++) {
    skip += fgetc(f);
    size = fgetc(f);
    if (!size) size = 256;

    for (c=skip; c<skip+size; c++) {
      r = fgetc(f);
      g = fgetc(f);
      b = fgetc(f);
      pal->setEntry(c, _rgba(_rgb_scale_6[r],
                             _rgb_scale_6[g],
                             _rgb_scale_6[b], 255));
    }
  }

  return pal;
}

static Palette *ase_file_read_color2_chunk(FILE *f, Sprite *sprite, FrameNumber frame)
{
  int i, c, r, g, b, packets, skip, size;
  Palette* pal = new Palette(*sprite->getPalette(frame));
  pal->setFrame(frame);

  packets = fgetw(f);   // Number of packets
  skip = 0;

  // Read all packets
  for (i=0; i<packets; i++) {
    skip += fgetc(f);
    size = fgetc(f);
    if (!size) size = 256;

    for (c=skip; c<skip+size; c++) {
      r = fgetc(f);
      g = fgetc(f);
      b = fgetc(f);
      pal->setEntry(c, _rgba(r, g, b, 255));
    }
  }

  return pal;
}

/* writes the original color chunk in FLI files for the entire palette "pal" */
static void ase_file_write_color2_chunk(FILE *f, Palette *pal)
{
  int c, color;

  ase_file_write_start_chunk(f, ASE_FILE_CHUNK_FLI_COLOR2);

  fputw(1, f);                  // number of packets

  // First packet
  fputc(0, f);                                   // skip 0 colors
  fputc(pal->size() == 256 ? 0: pal->size(), f); // number of colors
  for (c=0; c<pal->size(); c++) {
    color = pal->getEntry(c);

    fputc(_rgba_getr(color), f);
    fputc(_rgba_getg(color), f);
    fputc(_rgba_getb(color), f);
  }

  ase_file_write_close_chunk(f);
}

static Layer *ase_file_read_layer_chunk(FILE *f, Sprite *sprite, Layer **previous_layer, int *current_level)
{
  std::string name;
  Layer *layer = NULL;
  /* read chunk data */
  int flags;
  int layer_type;
  int child_level;

  flags = fgetw(f);
  layer_type = fgetw(f);
  child_level = fgetw(f);
  fgetw(f);                     // default width
  fgetw(f);                     // default height
  fgetw(f);                     // blend mode

  ase_file_read_padding(f, 4);
  name = ase_file_read_string(f);

  /* image layer */
  if (layer_type == 0) {
    layer = new LayerImage(sprite);
  }
  /* layer set */
  else if (layer_type == 1) {
    layer = new LayerFolder(sprite);
  }

  if (layer) {
    // flags
    layer->setFlags(flags);

    // name
    layer->setName(name.c_str());

    // child level...
    if (child_level == *current_level)
      (*previous_layer)->getParent()->addLayer(layer);
    else if (child_level > *current_level)
      static_cast<LayerFolder*>(*previous_layer)->addLayer(layer);
    else if (child_level < *current_level)
      (*previous_layer)->getParent()->getParent()->addLayer(layer);

    *previous_layer = layer;
    *current_level = child_level;
  }

  return layer;
}

static void ase_file_write_layer_chunk(FILE *f, Layer *layer)
{
  ase_file_write_start_chunk(f, ASE_FILE_CHUNK_LAYER);

  // Flags
  fputw(layer->getFlags(), f);

  /* layer type */
  fputw(layer->isImage() ? 0: (layer->isFolder() ? 1: -1), f);

  /* layer child level */
  LayerFolder* parent = layer->getParent();
  int child_level = -1;
  while (parent != NULL) {
    child_level++;
    parent = parent->getParent();
  }
  fputw(child_level, f);

  /* default width & height, and blend mode */
  fputw(0, f);
  fputw(0, f);
  fputw(layer->isImage() ? static_cast<LayerImage*>(layer)->getBlendMode(): 0, f);

  /* padding */
  ase_file_write_padding(f, 4);

  /* layer name */
  ase_file_write_string(f, layer->getName());

  ase_file_write_close_chunk(f);

  /* fop_error(fop, "Layer name \"%s\" child level: %d\n", layer->name, child_level); */
}

//////////////////////////////////////////////////////////////////////
// Pixel I/O
//////////////////////////////////////////////////////////////////////

template<typename ImageTraits>
class PixelIO
{
public:
  typename ImageTraits::pixel_t read_pixel(FILE* f);
  void write_pixel(FILE* f, typename ImageTraits::pixel_t c);
  void read_scanline(typename ImageTraits::address_t address, int w, uint8_t* buffer);
  void write_scanline(typename ImageTraits::address_t address, int w, uint8_t* buffer);
};

template<>
class PixelIO<RgbTraits>
{
  int r, g, b, a;
public:
  RgbTraits::pixel_t read_pixel(FILE* f) {
    r = fgetc(f);
    g = fgetc(f);
    b = fgetc(f);
    a = fgetc(f);
    return _rgba(r, g, b, a);
  }
  void write_pixel(FILE* f, RgbTraits::pixel_t c) {
    fputc(_rgba_getr(c), f);
    fputc(_rgba_getg(c), f);
    fputc(_rgba_getb(c), f);
    fputc(_rgba_geta(c), f);
  }
  void read_scanline(RgbTraits::address_t address, int w, uint8_t* buffer)
  {
    for (int x=0; x<w; ++x) {
      r = *(buffer++);
      g = *(buffer++);
      b = *(buffer++);
      a = *(buffer++);
      *(address++) = _rgba(r, g, b, a);
    }
  }
  void write_scanline(RgbTraits::address_t address, int w, uint8_t* buffer)
  {
    for (int x=0; x<w; ++x) {
      *(buffer++) = _rgba_getr(*address);
      *(buffer++) = _rgba_getg(*address);
      *(buffer++) = _rgba_getb(*address);
      *(buffer++) = _rgba_geta(*address);
      ++address;
    }
  }
};

template<>
class PixelIO<GrayscaleTraits>
{
  int k, a;
public:
  GrayscaleTraits::pixel_t read_pixel(FILE* f) {
    k = fgetc(f);
    a = fgetc(f);
    return _graya(k, a);
  }
  void write_pixel(FILE* f, GrayscaleTraits::pixel_t c) {
    fputc(_graya_getv(c), f);
    fputc(_graya_geta(c), f);
  }
  void read_scanline(GrayscaleTraits::address_t address, int w, uint8_t* buffer)
  {
    for (int x=0; x<w; ++x) {
      k = *(buffer++);
      a = *(buffer++);
      *(address++) = _graya(k, a);
    }
  }
  void write_scanline(GrayscaleTraits::address_t address, int w, uint8_t* buffer)
  {
    for (int x=0; x<w; ++x) {
      *(buffer++) = _graya_getv(*address);
      *(buffer++) = _graya_geta(*address);
      ++address;
    }
  }
};

template<>
class PixelIO<IndexedTraits>
{
public:
  IndexedTraits::pixel_t read_pixel(FILE* f) {
    return fgetc(f);
  }
  void write_pixel(FILE* f, IndexedTraits::pixel_t c) {
    fputc(c, f);
  }
  void read_scanline(IndexedTraits::address_t address, int w, uint8_t* buffer)
  {
    memcpy(address, buffer, w);
  }
  void write_scanline(IndexedTraits::address_t address, int w, uint8_t* buffer)
  {
    memcpy(buffer, address, w);
  }
};

//////////////////////////////////////////////////////////////////////
// Raw Image
//////////////////////////////////////////////////////////////////////

template<typename ImageTraits>
static void read_raw_image(FILE* f, Image* image, FileOp* fop, ASE_Header* header)
{
  PixelIO<ImageTraits> pixel_io;
  int x, y;

  for (y=0; y<image->h; y++) {
    for (x=0; x<image->w; x++)
      image_putpixel_fast<ImageTraits>(image, x, y, pixel_io.read_pixel(f));

    fop_progress(fop, (float)ftell(f) / (float)header->size);
  }
}

template<typename ImageTraits>
static void write_raw_image(FILE* f, Image* image)
{
  PixelIO<ImageTraits> pixel_io;
  int x, y;

  for (y=0; y<image->h; y++)
    for (x=0; x<image->w; x++)
      pixel_io.write_pixel(f, image_getpixel_fast<ImageTraits>(image, x, y));
}

//////////////////////////////////////////////////////////////////////
// Compressed Image
//////////////////////////////////////////////////////////////////////

template<typename ImageTraits>
static void read_compressed_image(FILE* f, Image* image, size_t chunk_end, FileOp* fop, ASE_Header* header)
{
  PixelIO<ImageTraits> pixel_io;
  z_stream zstream;
  int y, err;

  zstream.zalloc = (alloc_func)0;
  zstream.zfree  = (free_func)0;
  zstream.opaque = (voidpf)0;

  err = inflateInit(&zstream);
  if (err != Z_OK)
    throw base::Exception("ZLib error %d in inflateInit().", err);

  std::vector<uint8_t> scanline(ImageTraits::scanline_size(image->w));
  std::vector<uint8_t> uncompressed(image->h * ImageTraits::scanline_size(image->w));
  std::vector<uint8_t> compressed(4096);
  int uncompressed_offset = 0;

  while (true) {
    size_t input_bytes;

    if (ftell(f)+compressed.size() > chunk_end) {
      input_bytes = chunk_end - ftell(f); // Remaining bytes
      ASSERT(input_bytes < compressed.size());

      if (input_bytes == 0)
        break;                  // Done, we consumed all chunk
    }
    else
      input_bytes = compressed.size();

    size_t bytes_read = fread(&compressed[0], 1, input_bytes, f);
    zstream.next_in = (Bytef*)&compressed[0];
    zstream.avail_in = bytes_read;

    do {
      zstream.next_out = (Bytef*)&scanline[0];
      zstream.avail_out = scanline.size();

      err = inflate(&zstream, Z_NO_FLUSH);
      if (err != Z_OK && err != Z_STREAM_END && err != Z_BUF_ERROR)
        throw base::Exception("ZLib error %d in inflate().", err);

      size_t uncompressed_bytes = scanline.size() - zstream.avail_out;
      if (uncompressed_bytes > 0) {
        if (uncompressed_offset+uncompressed_bytes > uncompressed.size())
          throw base::Exception("Bad compressed image.");

        std::copy(scanline.begin(), scanline.begin()+uncompressed_bytes,
                  uncompressed.begin()+uncompressed_offset);

        uncompressed_offset += uncompressed_bytes;
      }
    } while (zstream.avail_out == 0);

    fop_progress(fop, (float)ftell(f) / (float)header->size);
  }

  uncompressed_offset = 0;
  for (y=0; y<image->h; y++) {
    typename ImageTraits::address_t address = image_address_fast<ImageTraits>(image, 0, y);
    pixel_io.read_scanline(address, image->w, &uncompressed[uncompressed_offset]);

    uncompressed_offset += ImageTraits::scanline_size(image->w);
  }

  err = inflateEnd(&zstream);
  if (err != Z_OK)
    throw base::Exception("ZLib error %d in inflateEnd().", err);
}

template<typename ImageTraits>
static void write_compressed_image(FILE* f, Image* image)
{
  PixelIO<ImageTraits> pixel_io;
  z_stream zstream;
  int y, err;

  zstream.zalloc = (alloc_func)0;
  zstream.zfree  = (free_func)0;
  zstream.opaque = (voidpf)0;
  err = deflateInit(&zstream, Z_DEFAULT_COMPRESSION);
  if (err != Z_OK)
    throw base::Exception("ZLib error %d in deflateInit().", err);

  std::vector<uint8_t> scanline(ImageTraits::scanline_size(image->w));
  std::vector<uint8_t> compressed(4096);

  for (y=0; y<image->h; y++) {
    typename ImageTraits::address_t address = image_address_fast<ImageTraits>(image, 0, y);
    pixel_io.write_scanline(address, image->w, &scanline[0]);

    zstream.next_in = (Bytef*)&scanline[0];
    zstream.avail_in = scanline.size();
    int flush = (y == image->h-1 ? Z_FINISH: Z_NO_FLUSH);

    do {
      zstream.next_out = (Bytef*)&compressed[0];
      zstream.avail_out = compressed.size();

      // Compress
      err = deflate(&zstream, flush);
      if (err != Z_OK && err != Z_STREAM_END && err != Z_BUF_ERROR)
        throw base::Exception("ZLib error %d in deflate().", err);

      int output_bytes = compressed.size() - zstream.avail_out;
      if (output_bytes > 0) {
        if ((fwrite(&compressed[0], 1, output_bytes, f) != (size_t)output_bytes)
            || ferror(f))
          throw base::Exception("Error writing compressed image pixels.\n");
      }
    } while (zstream.avail_out == 0);
  }

  err = deflateEnd(&zstream);
  if (err != Z_OK)
    throw base::Exception("ZLib error %d in deflateEnd().", err);
}

//////////////////////////////////////////////////////////////////////
// Cel Chunk
//////////////////////////////////////////////////////////////////////

static Cel *ase_file_read_cel_chunk(FILE *f, Sprite *sprite, FrameNumber frame,
                                    PixelFormat pixelFormat,
                                    FileOp *fop, ASE_Header *header, size_t chunk_end)
{
  /* read chunk data */
  LayerIndex layer_index = LayerIndex(fgetw(f));
  int x = ((short)fgetw(f));
  int y = ((short)fgetw(f));
  int opacity = fgetc(f);
  int cel_type = fgetw(f);
  Layer* layer;

  ase_file_read_padding(f, 7);

  layer = sprite->indexToLayer(layer_index);
  if (!layer) {
    fop_error(fop, "Frame %d didn't found layer with index %d\n",
              (int)frame, layer_index);
    return NULL;
  }
  if (!layer->isImage()) {
    fop_error(fop, "Invalid .ase file (frame %d in layer %d which does not contain images\n",
              (int)frame, layer_index);
    return NULL;
  }

  // Create the new frame.
  UniquePtr<Cel> cel(new Cel(frame, 0));
  cel->setPosition(x, y);
  cel->setOpacity(opacity);

  switch (cel_type) {

    case ASE_FILE_RAW_CEL: {
      // Read width and height
      int w = fgetw(f);
      int h = fgetw(f);

      if (w > 0 && h > 0) {
        Image* image = Image::create(pixelFormat, w, h);

        // Read pixel data
        switch (image->getPixelFormat()) {

          case IMAGE_RGB:
            read_raw_image<RgbTraits>(f, image, fop, header);
            break;

          case IMAGE_GRAYSCALE:
            read_raw_image<GrayscaleTraits>(f, image, fop, header);
            break;

          case IMAGE_INDEXED:
            read_raw_image<IndexedTraits>(f, image, fop, header);
            break;
        }

        cel->setImage(sprite->getStock()->addImage(image));
      }
      break;
    }

    case ASE_FILE_LINK_CEL: {
      // Read link position
      FrameNumber link_frame = FrameNumber(fgetw(f));
      Cel* link = static_cast<LayerImage*>(layer)->getCel(link_frame);

      if (link) {
        // Create a copy of the linked cel (avoid using links cel)
        Image* image = Image::createCopy(sprite->getStock()->getImage(link->getImage()));
        cel->setImage(sprite->getStock()->addImage(image));
      }
      else {
        // Linked cel doesn't found
        return NULL;
      }
      break;
    }

    case ASE_FILE_COMPRESSED_CEL: {
      // Read width and height
      int w = fgetw(f);
      int h = fgetw(f);

      if (w > 0 && h > 0) {
        Image* image = Image::create(pixelFormat, w, h);

        // Try to read pixel data
        try {
          switch (image->getPixelFormat()) {

            case IMAGE_RGB:
              read_compressed_image<RgbTraits>(f, image, chunk_end, fop, header);
              break;

            case IMAGE_GRAYSCALE:
              read_compressed_image<GrayscaleTraits>(f, image, chunk_end, fop, header);
              break;

            case IMAGE_INDEXED:
              read_compressed_image<IndexedTraits>(f, image, chunk_end, fop, header);
              break;
          }
        }
        // OK, in case of error we can show the problem, but continue
        // loading more cels.
        catch (const std::exception& e) {
          fop_error(fop, e.what());
        }

        cel->setImage(sprite->getStock()->addImage(image));
      }
      break;
    }

  }

  Cel* newCel = cel.release();
  static_cast<LayerImage*>(layer)->addCel(newCel);
  return newCel;
}

static void ase_file_write_cel_chunk(FILE *f, Cel *cel, LayerImage *layer, Sprite *sprite)
{
  int layer_index = sprite->layerToIndex(layer);
  int cel_type = ASE_FILE_COMPRESSED_CEL;

  ase_file_write_start_chunk(f, ASE_FILE_CHUNK_CEL);

  fputw(layer_index, f);
  fputw(cel->getX(), f);
  fputw(cel->getY(), f);
  fputc(cel->getOpacity(), f);
  fputw(cel_type, f);
  ase_file_write_padding(f, 7);

  switch (cel_type) {

    case ASE_FILE_RAW_CEL: {
      Image* image = sprite->getStock()->getImage(cel->getImage());

      if (image) {
        // Width and height
        fputw(image->w, f);
        fputw(image->h, f);

        // Pixel data
        switch (image->getPixelFormat()) {

          case IMAGE_RGB:
            write_raw_image<RgbTraits>(f, image);
            break;

          case IMAGE_GRAYSCALE:
            write_raw_image<GrayscaleTraits>(f, image);
            break;

          case IMAGE_INDEXED:
            write_raw_image<IndexedTraits>(f, image);
            break;
        }
      }
      else {
        // Width and height
        fputw(0, f);
        fputw(0, f);
      }
      break;
    }

    case ASE_FILE_LINK_CEL:
      // Linked cel to another frame
      //fputw(link->frame, f);
      fputw(0, f);
      break;

    case ASE_FILE_COMPRESSED_CEL: {
      Image* image = sprite->getStock()->getImage(cel->getImage());

      if (image) {
        // Width and height
        fputw(image->w, f);
        fputw(image->h, f);

        // Pixel data
        switch (image->getPixelFormat()) {

          case IMAGE_RGB:
            write_compressed_image<RgbTraits>(f, image);
            break;

          case IMAGE_GRAYSCALE:
            write_compressed_image<GrayscaleTraits>(f, image);
            break;

          case IMAGE_INDEXED:
            write_compressed_image<IndexedTraits>(f, image);
            break;
        }
      }
      else {
        // Width and height
        fputw(0, f);
        fputw(0, f);
      }
      break;
    }
  }

  ase_file_write_close_chunk(f);
}

static Mask *ase_file_read_mask_chunk(FILE *f)
{
  int c, u, v, byte;
  Mask *mask;
  // Read chunk data
  int x = fgetw(f);
  int y = fgetw(f);
  int w = fgetw(f);
  int h = fgetw(f);

  ase_file_read_padding(f, 8);
  std::string name = ase_file_read_string(f);

  mask = new Mask();
  mask->setName(name.c_str());
  mask->replace(x, y, w, h);

  // Read image data
  for (v=0; v<h; v++)
    for (u=0; u<(w+7)/8; u++) {
      byte = fgetc(f);
      for (c=0; c<8; c++)
        image_putpixel(mask->getBitmap(), u*8+c, v, byte & (1<<(7-c)));
    }

  return mask;
}

static void ase_file_write_mask_chunk(FILE *f, Mask *mask)
{
  int c, u, v, byte;
  const gfx::Rect& bounds(mask->getBounds());

  ase_file_write_start_chunk(f, ASE_FILE_CHUNK_MASK);

  fputw(bounds.x, f);
  fputw(bounds.y, f);
  fputw(bounds.w, f);
  fputw(bounds.h, f);
  ase_file_write_padding(f, 8);

  // Name
  ase_file_write_string(f, mask->getName().c_str());

  // Bitmap
  for (v=0; v<bounds.h; v++)
    for (u=0; u<(bounds.w+7)/8; u++) {
      byte = 0;
      for (c=0; c<8; c++)
        if (image_getpixel(mask->getBitmap(), u*8+c, v))
          byte |= (1<<(7-c));
      fputc(byte, f);
    }

  ase_file_write_close_chunk(f);
}

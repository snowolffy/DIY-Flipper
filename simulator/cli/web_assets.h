// web_assets.h - the window's files (simulator/web), compiled into the exe by cmake/embed_web.cmake so
// `sim` runs from a single file with no network.
#pragma once

#include <cstddef>

namespace simcli {

struct WebFile {
  const char* path;  // "index.html", "app.js", "fonts/x.woff2"
  const char* type;  // content type
  const unsigned char* data;
  size_t size;
};
extern const WebFile kWebFiles[];
extern const size_t kWebFileCount;

}  // namespace simcli

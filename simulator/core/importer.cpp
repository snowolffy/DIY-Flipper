#include "core/importer.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

#include "app/theme.h"
#include "miniz/miniz.h"

namespace fs = std::filesystem;

namespace sim {

namespace {

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
  return s;
}

bool readHostFile(const fs::path& p, std::string& out) {
  std::ifstream f(p, std::ios::binary);
  if (!f) return false;
  std::ostringstream ss;
  ss << f.rdbuf();
  out = ss.str();
  return true;
}

bool writeDeviceFile(MockStorage& st, hal::Volume v, const std::string& path, const std::string& data) {
  const fs::path host = st.hostPath(v, path);
  if (host.empty()) return false;
  std::error_code ec;
  fs::create_directories(host.parent_path(), ec);
  std::ofstream f(host, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(data.data(), (std::streamsize)data.size());
  return (bool)f;
}

// Zip entry names are untrusted: no absolute paths, no "..", forward slashes only.
bool safeEntry(const std::string& name) {
  if (name.empty() || name[0] == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos)
    return false;
  std::stringstream ss(name);
  std::string part;
  while (std::getline(ss, part, '/'))
    if (part == "..") return false;
  return true;
}

// Theme folder names become one path segment on the card.
std::string cleanName(const std::string& s) {
  std::string out;
  for (char c : s) out += (std::isalnum((unsigned char)c) || c == '_' || c == '-') ? (char)std::tolower((unsigned char)c) : '_';
  return out.empty() ? "theme" : out;
}

ImportResult importTheme(const fs::path& file, MockStorage& st) {
  ImportResult r;
  // read it ourselves so non-ASCII Windows paths work (miniz would fopen the narrow path)
  std::string archive;
  if (!readHostFile(file, archive)) {
    r.error = "can't read " + file.string();
    return r;
  }
  mz_zip_archive zip{};
  if (!mz_zip_reader_init_mem(&zip, archive.data(), archive.size(), 0)) {
    r.error = "not a readable .zip file";
    return r;
  }

  // every entry, plus the folders that hold a theme.ini
  std::vector<std::string> names;
  std::map<std::string, std::string> themeDirs;  // zip prefix ("system/theme/night/") -> theme name
  const mz_uint n = mz_zip_reader_get_num_files(&zip);
  for (mz_uint i = 0; i < n; i++) {
    char name[512];
    mz_zip_reader_get_filename(&zip, i, name, sizeof(name));
    names.emplace_back(name);
    const std::string s = name;
    const size_t slash = s.rfind('/');
    const std::string base = slash == std::string::npos ? s : s.substr(slash + 1);
    if (lower(base) != "theme.ini" || !safeEntry(s)) continue;
    const std::string prefix = slash == std::string::npos ? "" : s.substr(0, slash + 1);
    std::string folder = prefix.empty() ? file.stem().string() : prefix.substr(0, prefix.size() - 1);
    if (folder.find('/') != std::string::npos) folder = folder.substr(folder.rfind('/') + 1);
    themeDirs[prefix] = cleanName(folder);
  }
  if (themeDirs.empty()) {
    mz_zip_reader_end(&zip);
    r.error = "no theme.ini in the zip - export a theme pack from Flipper UI Studio's Theme pack tab";
    return r;
  }

  for (const auto& td : themeDirs) {
    const std::string& prefix = td.first;
    const std::string& theme = td.second;
    const std::string dest = std::string(theme::kRoot) + "/" + theme;
    for (mz_uint i = 0; i < n; i++) {
      const std::string& name = names[i];
      if (name.compare(0, prefix.size(), prefix) != 0 || mz_zip_reader_is_file_a_directory(&zip, i)) continue;
      const std::string rel = name.substr(prefix.size());
      if (rel.empty()) continue;
      if (!safeEntry(name)) {
        r.warnings.push_back("skipped " + name + ": path leaves the theme folder");
        continue;
      }
      size_t size = 0;
      void* data = mz_zip_reader_extract_to_heap(&zip, i, &size, 0);
      if (!data) {
        r.warnings.push_back("skipped " + name + ": couldn't decompress");
        continue;
      }
      const std::string bytes(static_cast<const char*>(data), size);
      mz_free(data);
      if (!writeDeviceFile(st, hal::Volume::Sd, dest + "/" + rel, bytes)) {
        mz_zip_reader_end(&zip);
        r.error = "couldn't write " + dest + "/" + rel;
        return r;
      }
      r.written.push_back("sd:" + dest + "/" + rel);
    }
    r.themes.push_back(theme);

    // same checks the firmware runs when the theme is picked (needs the card "inserted" to read it back)
    if (st.sdPresent()) {
      std::string err;
      std::vector<std::string> warnings;
      if (!theme::validate(st, theme, err, warnings)) r.warnings.push_back(theme + ": " + err);
      for (const auto& w : warnings) r.warnings.push_back(theme + ": " + w);
    } else {
      r.warnings.push_back(theme + ": not checked because the SD card is out");
    }
  }
  mz_zip_reader_end(&zip);
  r.ok = true;
  return r;
}

}  // namespace

ImportResult importAsset(const fs::path& file, MockStorage& st) {
  ImportResult r;
  const std::string ext = lower(file.extension().string());
  if (ext == ".zip") return importTheme(file, st);

  if (ext != ".b1i" && ext != ".b1f") {
    r.error = "unsupported file type '" + ext + "' - import a .b1i, .b1f or theme .zip";
    return r;
  }
  std::string bytes;
  if (!readHostFile(file, bytes)) {
    r.error = "can't read " + file.string();
    return r;
  }
  std::string err;
  std::string dest;
  if (ext == ".b1i") {
    theme::Image img;
    if (!theme::parseB1i(bytes, img, err)) {
      r.error = err;
      return r;
    }
    dest = "/media/" + file.filename().string();
  } else {
    theme::Font font;
    if (!theme::parseB1f(bytes, font, err)) {
      r.error = err;
      return r;
    }
    dest = "/system/fonts/" + file.filename().string();
  }
  if (!writeDeviceFile(st, hal::Volume::Sd, dest, bytes)) {
    r.error = "couldn't write sd:" + dest;
    return r;
  }
  r.written.push_back("sd:" + dest);
  r.ok = true;
  return r;
}

}  // namespace sim

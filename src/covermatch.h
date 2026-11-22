// Birdstation - matches ROM file names to libretro-thumbnails box art names (portable, no SDL)
#pragma once
#include <string>
#include <vector>

namespace covermatch {

struct Entry {
  std::string name;  // decoded name without ".png", e.g. "Super Mario World (USA)"
  std::string href;  // path segment as listed by the server (already URL-encoded)
  std::string key;   // normalized title used for matching
};

// Extracts "*.png" links from the server's directory listing (HTML).
std::vector<Entry> ParseIndex(const std::string& html);

// Extracts "Named_Boxarts/*.png" paths from the GitHub git-tree API JSON of the
// libretro-thumbnails repository (https://api.github.com/repos/.../git/trees/master?recursive=1).
std::vector<Entry> ParseGithubTree(const std::string& json, const std::string& folder = "Named_Boxarts/");

// Percent-encodes a name for a URL path (no character replacement).
std::string UrlEncode(const std::string& name);

// Normalized title: text before the first '(' or '[', lower case, punctuation and the words
// "the"/"and" removed. "Legend of Zelda, The - A Link to the Past (USA)" and
// "The Legend of Zelda - A Link to the Past (U) [!]" give the same key.
std::string Key(const std::string& name);

// Best entry for a ROM file name (without extension), or -1. Prefers an exact name, then the same
// region as the ROM, then USA > World > Europe > Japan, and avoids Beta/Proto/Demo variants.
int Pick(const std::vector<Entry>& entries, const std::string& romStem);

// Escapes a name the way libretro-thumbnails stores it and percent-encodes it for a URL path.
std::string UrlName(std::string name);

}  // namespace covermatch

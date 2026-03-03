#pragma once

#include "archive/archive.hpp"
#include "utils/progress.hpp"

#include <string>
#include <vector>

namespace zipbox::extraction {

void ExtractArchive(const std::string& filename,
                    const std::string& destination,
                    const std::string& password = "",
                    const utils::ProgressCallback& progressCallback = {});
std::vector<zipbox::archive::ArchiveEntryInfo> ListArchiveEntries(const std::string& filename,
                                                                  const std::string& password = "");
zipbox::archive::ArchiveMetadata GetArchiveMetadata(const std::string& filename, const std::string& password = "");
std::vector<uint8_t> ReadArchiveEntry(const std::string& filename,
                                      size_t entryIndex,
                                      const std::string& password = "");
void TestArchive(const std::string& filename, const std::string& password = "");

} // namespace zipbox::extraction

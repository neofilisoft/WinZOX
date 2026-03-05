#pragma once

#include "archive/archive.hpp"
#include "utils/progress.hpp"

#include <string>
#include <vector>

namespace winzox::extraction {

void ExtractArchive(const std::string& filename,
                    const std::string& destination,
                    const std::string& password = "",
                    const utils::ProgressCallback& progressCallback = {});
std::vector<winzox::archive::ArchiveEntryInfo> ListArchiveEntries(const std::string& filename,
                                                                  const std::string& password = "");
winzox::archive::ArchiveMetadata GetArchiveMetadata(const std::string& filename, const std::string& password = "");
std::vector<uint8_t> ReadArchiveEntry(const std::string& filename,
                                      size_t entryIndex,
                                      const std::string& password = "");
void TestArchive(const std::string& filename, const std::string& password = "");

} // namespace winzox::extraction

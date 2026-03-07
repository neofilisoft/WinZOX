#pragma once

#include "archive/archive.hpp"
#include "utils/progress.hpp"

#include <functional>
#include <string>
#include <vector>

namespace winzox::extraction {

enum class OverwriteAction {
    Replace,
    Rename,
    ReplaceAll,
    Cancel,
};

struct OverwriteDecision {
    OverwriteAction action = OverwriteAction::Replace;
    std::string renamedPath;
};

using OverwriteCallback = std::function<OverwriteDecision(const std::string& existingPath,
                                                          const std::string& archiveEntryPath)>;

void ExtractArchive(const std::string& filename,
                    const std::string& destination,
                    const std::string& password = "",
                    const utils::ProgressCallback& progressCallback = {},
                    const OverwriteCallback& overwriteCallback = {});
std::vector<winzox::archive::ArchiveEntryInfo> ListArchiveEntries(const std::string& filename,
                                                                  const std::string& password = "");
winzox::archive::ArchiveMetadata GetArchiveMetadata(const std::string& filename, const std::string& password = "");
std::vector<uint8_t> ReadArchiveEntry(const std::string& filename,
                                      size_t entryIndex,
                                      const std::string& password = "");
void TestArchive(const std::string& filename, const std::string& password = "");

} // namespace winzox::extraction

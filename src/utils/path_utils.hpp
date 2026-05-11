#pragma once

#include <filesystem>
#include <string>

namespace winzox::utils {

std::string ToLower(std::string value);
bool IsSplitZoxExtension(const std::string& extension);
std::filesystem::path EnsurePathWithinRoot(const std::filesystem::path& destinationRoot,
                                           const std::filesystem::path& candidatePath,
                                           const std::string& sourcePath = {});
std::filesystem::path ResolveSafeOutputPath(const std::filesystem::path& destinationRoot,
                                            const std::string& entryPath);

} // namespace winzox::utils

#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

#include "core/ModManifest.h"

namespace mods {

// Parses the JSON published on the "distribution" branch. Unknown members are
// ignored, so a newer manifest keeps working. Returns an empty value and fills
// "error" when the document cannot be understood at all; entries with missing
// optional fields are still returned and validated where they are used.
std::optional<core::ModManifest> ParseModManifest(const QByteArray& json, QString& error);

} // namespace mods

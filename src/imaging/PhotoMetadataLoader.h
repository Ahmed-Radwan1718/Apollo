#pragma once

#include "imaging/PhotoMetadata.h"

#include <QString>

namespace imaging {

class PhotoMetadataLoader final
{
public:
    PhotoMetadataLoader() = delete;

    // Reads lightweight photo metadata without decoding image pixels.
    [[nodiscard]] static PhotoMetadata load(const QString &filePath);
};

} // namespace imaging

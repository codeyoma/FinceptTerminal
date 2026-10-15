#pragma once

#include <QStringList>

namespace fincept::screens {

/// The colours an indicator's line (krx-quant #222) or a drawing (krx-quant #224) can take: the
/// indicators' defaults, the drawings' (#60A5FA) and a few more, in the order of their palettes.
inline const QStringList& krx_swatches() {
    static const QStringList colors{"#F472B6", "#F59E0B", "#22C55E", "#38BDF8", "#C084FC", "#FB923C",
                                    "#2DD4BF", "#60A5FA", "#EF4444", "#FACC15", "#A3E635", "#E879F9",
                                    "#3B82F6", "#A78BFA", "#86EFAC", "#FCA5A5", "#94A3B8", "#F5F5F5"};
    return colors;
}

} // namespace fincept::screens

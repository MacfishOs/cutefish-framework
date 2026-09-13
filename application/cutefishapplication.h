#pragma once

namespace Cutefish::Application {

// Call before the first QML engine loads QtQuick.Controls, or the style is ignored.
void prepare();

} // namespace Cutefish::Application

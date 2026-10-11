#pragma once

// Defined by an application added with cutefish_add_application(): its main(),
// entered once the application object exists. cutefish-booster calls it in a
// process that constructed the application object before the launch.
extern "C" int cutefish_application_main(int argc, char *argv[]);

namespace Cutefish::Application {

using Entry = int (*)(int argc, char *argv[]);

// Set the shared application metadata before constructing the application.
void prepare();

// main() of an application started without the booster: constructs the
// application object the way the booster does, then enters the application.
int run(int &argc, char *argv[], Entry entry);

} // namespace Cutefish::Application

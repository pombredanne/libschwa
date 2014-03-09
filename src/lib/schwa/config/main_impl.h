/* -*- Mode: C++; indent-tabs-mode: nil -*- */
#ifndef SCHWA_CONFIG_MAIN_IMPL_H_
#define SCHWA_CONFIG_MAIN_IMPL_H_

#include <schwa/config/exception.h>
#include <schwa/io/logging.h>

namespace schwa {
  namespace config {

    template <typename LOGGER>
    void
    Main::main(int argc, char **argv) {
      // Store a copy of the full commandline arguments for debugging/reproducability purposes.
      _cmdline_args.clear();
      for (int i = 0; i != argc; ++i)
        _cmdline_args.push_back(argv[i]);

      // Process the command line arguments.
      try {
        if (!_main())
          std::exit(1);
      }
      catch (ConfigException &e) {
        std::cerr << print_exception("ConfigException", e) << std::endl;
        help(std::cerr);
        std::exit(1);
      }
      catch (IOException &e) {
        std::cerr << print_exception("IOException", e) << std::endl;
        help(std::cerr);
        std::exit(1);
      }

      // Configure logging.
      io::default_logger = new LOGGER((*_log).file());
      io::default_logger->threshold((*_log_level)());
    }

  }
}

#endif  // SCHWA_CONFIG_MAIN_IMPL_H_
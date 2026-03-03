// SPDX-License-Identifier: MIT
// edge-healthd: Entry point

#include "config.hpp"
#include "daemon.hpp"
#include "log.hpp"
#include "version.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <unistd.h>

namespace {

// ANSI helpers for stdout (separate from log's stderr detection)
struct HelpColors {
    const char* bold;
    const char* dim;
    const char* cyan;
    const char* byellow;  // bold yellow
    const char* ul;       // underline
    const char* reset;
};

HelpColors help_colors() {
    if (::isatty(STDOUT_FILENO)) {
        return {"\033[1m", "\033[2m", "\033[36m", "\033[1;33m", "\033[4m", "\033[0m"};
    }
    return {"", "", "", "", "", ""};
}

void print_usage([[maybe_unused]] std::string_view program) {
    auto c = help_colors();
    std::cout
        << c.bold << "edge-healthd" << c.reset << " " << c.dim << EDGE_HEALTHD_VERSION << c.reset << "\n"
        << c.dim << "Edge device health monitoring daemon" << c.reset << "\n"
        << "\n"
        << c.byellow << "USAGE:" << c.reset << "\n"
        << "    " << c.bold << "edge-healthd" << c.reset << " " << c.cyan << "[OPTIONS]" << c.reset << "\n"
        << "\n"
        << c.byellow << "OPTIONS:" << c.reset << "\n"
        << "    " << c.cyan << "-h" << c.reset << ", " << c.cyan << "--help" << c.reset << "              Show this help message and exit\n"
        << "    " << c.cyan << "--version" << c.reset << "               Show version information\n"
        << "    " << c.cyan << "-f" << c.reset << ", " << c.cyan << "--foreground" << c.reset << "        Run in foreground (don't daemonize)\n"
        << "    " << c.cyan << "-v" << c.reset << ", " << c.cyan << "--verbose" << c.reset << "           Enable debug logging\n"
        << "    " << c.cyan << "-c" << c.reset << ", " << c.cyan << "--config" << c.reset << " " << c.ul << "FILE" << c.reset << "      Configuration file " << c.dim << "[default: /etc/edge/healthd.conf]" << c.reset << "\n"
        << "    " << c.cyan << "--dump-config" << c.reset << "           Print effective configuration and exit\n"
        << "    " << c.cyan << "--once" << c.reset << "                  Collect a single snapshot and exit\n";
}

void print_version() {
    auto c = help_colors();
    std::cout << c.bold << "edge-healthd" << c.reset << " " << c.dim << EDGE_HEALTHD_VERSION << c.reset << "\n";
}

struct Args {
    std::string config_file = "/etc/edge/healthd.conf";
    bool foreground = false;
    bool verbose = false;
    bool once = false;
    bool dump_config = false;
    bool help = false;
    bool version = false;
};

edge::log::Level parse_log_level(std::string_view level) {
    if (level == "debug") {
        return edge::log::Level::Debug;
    }
    if (level == "warn") {
        return edge::log::Level::Warn;
    }
    if (level == "error") {
        return edge::log::Level::Error;
    }
    return edge::log::Level::Info;
}

Args parse_args(int argc, char* argv[]) {
    Args args;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            args.help = true;
        } else if (arg == "--version") {
            args.version = true;
        } else if (arg == "-f" || arg == "--foreground") {
            args.foreground = true;
        } else if (arg == "-v" || arg == "--verbose") {
            args.verbose = true;
        } else if (arg == "--once") {
            args.once = true;
        } else if (arg == "--dump-config") {
            args.dump_config = true;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            args.config_file = argv[++i];
        }
    }

    return args;
}

} // namespace

int main(int argc, char* argv[]) {
    edge::log::init();

    auto args = parse_args(argc, argv);

    if (args.help) {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    }

    if (args.version) {
        print_version();
        return EXIT_SUCCESS;
    }

    // Load configuration
    edge::Config config;
    std::string config_warning;
    bool has_warning = false;
    if (std::filesystem::exists(args.config_file)) {
        try {
            config = edge::Config::load(args.config_file);
        } catch (const std::exception& ex) {
            config_warning = "Failed to load config file: " + args.config_file +
                " (" + ex.what() + "); using defaults";
            has_warning = true;
            config = edge::Config::defaults();
        }
    } else {
        config_warning = "Config file not found: " + args.config_file +
            "; using defaults";
        has_warning = true;
        config = edge::Config::defaults();
    }
    if (has_warning) {
        std::cerr << "Warning: " << config_warning << "\n";
    }
    config.foreground = args.foreground;
    config.verbose = args.verbose;
    if (args.verbose) {
        config.log_level = "debug";
    }
    edge::log::set_level(parse_log_level(config.log_level));
#ifdef EDGE_HAS_SYSTEMD
    if (has_warning) {
        edge::log::warn(config_warning);
    }
#endif

    if (auto err = config.validate()) {
        std::cerr << "Configuration error: " << *err << "\n";
        return EXIT_FAILURE;
    }

    if (args.dump_config) {
        std::cout << config.to_json_string(2) << "\n";
        return EXIT_SUCCESS;
    }

    // Create and run daemon
    edge::SnapshotDaemon daemon(std::move(config));

    if (auto err = daemon.initialize()) {
        std::cerr << "Initialization failed: " << *err << "\n";
        return EXIT_FAILURE;
    }

    if (args.once) {
        daemon.collect_now();
        return EXIT_SUCCESS;
    }

    return daemon.run();
}

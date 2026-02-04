// SPDX-License-Identifier: MIT
// edge-healthd: Entry point

#include "config.hpp"
#include "daemon.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace {

void print_usage(std::string_view program) {
    std::cout << "Usage: " << program << " [OPTIONS]\n"
              << "\n"
              << "Options:\n"
              << "  -c, --config FILE   Configuration file (default: /etc/edge/healthd.conf)\n"
              << "  -f, --foreground    Run in foreground (don't daemonize)\n"
              << "  -v, --verbose       Enable verbose logging\n"
              << "  --dump-config       Print effective configuration and exit\n"
              << "  --once              Collect once and exit\n"
              << "  -h, --help          Show this help\n"
              << "  --version           Show version\n";
}

void print_version() {
    std::cout << "edge-healthd 0.1.0\n";
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

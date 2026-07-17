// SPDX-License-Identifier: MIT
#include "journal.hpp"
#include <algorithm>
#include <cstdint>

namespace edge {

std::string sanitize_utf8(std::string_view input) {
    std::string out;
    out.reserve(input.size());

    const auto* p = reinterpret_cast<const unsigned char*>(input.data());
    const size_t n = input.size();

    auto append_replacement = [&out]() {
        // U+FFFD REPLACEMENT CHARACTER, encoded as UTF-8 (EF BF BD).
        out += '\xEF';
        out += '\xBF';
        out += '\xBD';
    };

    size_t i = 0;
    while (i < n) {
        const unsigned char c = p[i];

        // Fast path: 7-bit ASCII.
        if (c < 0x80) {
            out += static_cast<char>(c);
            ++i;
            continue;
        }

        // Decode the expected multibyte sequence length from the lead byte.
        size_t seq_len = 0;
        uint32_t cp = 0;
        uint32_t min_cp = 0;
        if ((c & 0xE0) == 0xC0) {
            seq_len = 2;
            cp = c & 0x1Fu;
            min_cp = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            seq_len = 3;
            cp = c & 0x0Fu;
            min_cp = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            seq_len = 4;
            cp = c & 0x07u;
            min_cp = 0x10000;
        } else {
            // Invalid lead byte (continuation byte or 5/6-byte form).
            append_replacement();
            ++i;
            continue;
        }

        // Not enough bytes left for a complete sequence (truncated tail).
        if (i + seq_len > n) {
            append_replacement();
            ++i;
            continue;
        }

        // Validate continuation bytes and accumulate the code point.
        bool ok = true;
        for (size_t k = 1; k < seq_len; ++k) {
            const unsigned char cc = p[i + k];
            if ((cc & 0xC0) != 0x80) {
                ok = false;
                break;
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }

        // Reject overlong encodings, UTF-16 surrogates, and out-of-range values.
        if (!ok || cp < min_cp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            append_replacement();
            ++i;
            continue;
        }

        // Well-formed: copy the bytes verbatim.
        out.append(reinterpret_cast<const char*>(p + i), seq_len);
        i += seq_len;
    }

    return out;
}

std::vector<std::string> filter_journal_entries(const Config& cfg,
                                                const std::vector<JournalEntry>& entries) {
    std::vector<std::string> out;
    if (entries.empty()) return out;

    // Determine window usage
    bool use_window = cfg.log_excerpt_window_sec && (*cfg.log_excerpt_window_sec > 0);
    uint64_t cutoff_usec = 0;
    if (use_window) {
        const auto now = std::chrono::system_clock::now();
        const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
        cutoff_usec = static_cast<uint64_t>(now_us) - (*cfg.log_excerpt_window_sec * 1000000ULL);
    }

    // Iterate entries (assume entries provided in reverse-chronological order (newest first))
    // We'll collect up to max_lines, applying priority and window filters, then reverse to chronological order.
    const size_t max_lines = cfg.log_excerpt_max_lines ? cfg.log_excerpt_max_lines : 20;
    for (const auto& e : entries) {
        if (use_window && e.realtime_usec < cutoff_usec) continue;
        if (cfg.log_excerpt_min_priority) {
            if (!(e.priority <= *cfg.log_excerpt_min_priority)) continue;
        }
        // Trim trailing newlines
        std::string msg = e.message;
        while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) msg.pop_back();
        // Truncate long lines to 512 chars
        if (msg.size() > 512) msg.resize(512);
        if (!msg.empty()) out.push_back(std::move(msg));
        if (out.size() >= max_lines) break;
    }

    // Entries are returned newest->oldest (most recent first)
    return out;
}

} // namespace edge

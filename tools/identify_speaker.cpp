/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 *
 * 说话人识别/验证 CLI 工具
 */

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include "../include/vp_service.h"

static void PrintRtf(const std::shared_ptr<SpacemiT::VpResult>& result) {
    std::cout << "RTF: "
        << std::fixed << std::setprecision(4)
        << result->GetRTF() << "\n";
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS] audio.wav\n";
    std::cout << "\nIdentify a speaker from an audio file.\n\n";
    std::cout << "Options:\n";
    std::cout << "  -d, --database FILE   Database file (default: speakers.db)\n";
    std::cout << "  -t, --threads NUM     Number of threads (default: 1)\n";
    std::cout << "  -s, --threshold VAL   Similarity threshold 0-1 (default: 0.6)\n";
    std::cout << "  -n, --top NUM         Show top N matches (default: 3)\n";
    std::cout << "  -v, --verify NAME     Verify if audio matches specific speaker\n";
    std::cout << "  -l, --list           List all registered speakers\n";
    std::cout << "  -V, --verbose        Show all similarity scores\n";
    std::cout << "  -h, --help           Show this help message\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << prog << " test.wav\n";
    std::cout << "  " << prog << " -n 5 test.wav\n";
    std::cout << "  " << prog << " -v john test.wav\n";
    std::cout << "  " << prog << " -l\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string database_file = "speakers.db";
    std::string audio_file;
    std::string verify_name;
    int num_threads = 1;
    float threshold = 0.6f;
    int top_n = 3;
    bool list_speakers = false;
    bool verbose = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--database") == 0) {
            if (i + 1 < argc) {
                database_file = argv[++i];
            } else {
                std::cerr << "Error: -d requires a database file path\n";
                return 1;
            }
        } else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) {
            if (i + 1 < argc) {
                num_threads = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: -t requires a number\n";
                return 1;
            }
        } else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--threshold") == 0) {
            if (i + 1 < argc) {
                threshold = std::stof(argv[++i]);
            } else {
                std::cerr << "Error: -s requires a threshold value\n";
                return 1;
            }
        } else if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "--top") == 0) {
            if (i + 1 < argc) {
                top_n = std::stoi(argv[++i]);
            } else {
                std::cerr << "Error: -n requires a number\n";
                return 1;
            }
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verify") == 0) {
            if (i + 1 < argc) {
                verify_name = argv[++i];
            } else {
                std::cerr << "Error: -v requires a speaker name\n";
                return 1;
            }
        } else if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--list") == 0) {
            list_speakers = true;
        } else if (strcmp(argv[i], "-V") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (argv[i][0] != '-') {
            audio_file = argv[i];
        }
    }

    // Create engine
    auto config = SpacemiT::VpConfig::Preset("campplus")
        .withNumThreads(num_threads)
        .withThreshold(threshold);

    SpacemiT::VpEngine engine(config);
    if (!engine.IsInitialized()) {
        std::cerr << "Failed to initialize engine\n";
        return 1;
    }

    // Load database
    if (!engine.LoadDatabase(database_file)) {
        std::cerr << "Warning: Could not load database from " << database_file << "\n";
        std::cerr << "Starting with empty database.\n";
    }

    // List speakers mode
    if (list_speakers) {
        auto speakers = engine.GetAllSpeakers();
        std::cout << "Registered speakers (" << speakers.size() << "):\n";
        for (const auto& name : speakers) {
            std::cout << "  - " << name << "\n";
        }
        return 0;
    }

    if (audio_file.empty()) {
        std::cerr << "Error: Audio file required for identification\n";
        print_usage(argv[0]);
        return 1;
    }

    // Verification mode
    if (!verify_name.empty()) {
        std::cout << "Processing: " << audio_file << "\n";
        auto result = engine.Verify(verify_name, audio_file);
        if (!result->IsSuccess()) {
            std::cerr << "Error: " << result->GetErrorMessage() << "\n";
            return 1;
        }
        PrintRtf(result);

        std::cout << "Verification result for '" << verify_name << "': ";
        if (result->IsVerified()) {
            std::cout << "VERIFIED (match, score: "
                << std::fixed << std::setprecision(3) << result->GetScore() << ")\n";
            return 0;
        } else {
            std::cout << "NOT VERIFIED (no match, score: "
                << std::fixed << std::setprecision(3) << result->GetScore() << ")\n";
            return 1;
        }
    }

    // Identification mode
    std::cout << "Processing: " << audio_file << "\n";
    std::cout << "Threshold: " << threshold << "\n\n";

    auto result = engine.Identify(audio_file);
    if (!result->IsSuccess()) {
        std::cerr << "Error: " << result->GetErrorMessage() << "\n";
        return 1;
    }
    PrintRtf(result);

    auto matches = result->GetMatches();
    if (matches.empty()) {
        std::cout << "No speakers in database\n";
        return 0;
    }

    // Find best match above threshold
    if (result->IsIdentified()) {
        std::cout << "IDENTIFIED: " << result->GetName() << " (score: "
            << std::fixed << std::setprecision(3) << result->GetScore() << ")\n";
    } else {
        std::cout << "UNKNOWN: No match above threshold\n";
    }

    // Show top matches
    int show_count = verbose ? static_cast<int>(matches.size()) : std::min(static_cast<int>(matches.size()), top_n);
    std::cout << "\nTop " << show_count << " matches:\n";
    for (int i = 0; i < show_count; i++) {
        std::cout << "  " << (i + 1) << ". " << matches[i].name
            << " (score: " << std::fixed << std::setprecision(3)
            << matches[i].score << ")";
        if (matches[i].score >= threshold) {
            std::cout << " *";
        }
        std::cout << "\n";
    }

    return 0;
}

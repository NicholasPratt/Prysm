// SPDX-License-Identifier: GPL-3.0-only
// The editor page inside the plugin (generated: plugin/cmake/embed.cmake)
#pragma once
#include <cstddef>
struct PrysmAsset { const char* path; const unsigned char* data; size_t size; const char* mime; };
extern const PrysmAsset prysm_assets[];
extern const size_t prysm_assets_n;

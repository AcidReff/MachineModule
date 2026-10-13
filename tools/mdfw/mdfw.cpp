// mdfw: inspect a Machinedrum OS .syx. Prints the container sections and the DSP record map of each section
// that decodes as DSP records; with --dump DIR also writes the decompressed sections (firmware: never commit them).
#include "Firmware.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace md::fw;

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: mdfw OS.syx [--records] [--dump DIR]\n"); return 2; }
    bool records = false; std::string dumpDir;
    for (int i = 2; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--records")) records = true;
        else if (!std::strcmp(argv[i], "--dump") && i + 1 < argc) dumpDir = argv[++i];
    }
    try {
        const auto img = parseSysex(readFile(argv[1]));
        std::printf("flash base $%06x, %zu bytes\n", img.base, img.bytes.size());
        const auto c = parseContainer(img);
        size_t end = 0;
        for (const auto& s : c.sections) end = s.flashAddr - img.base + 8 + s.streamSize;
        std::printf("version '%s', %zu sections, container ends at +%zu of %zu\n", c.version.c_str(), c.sections.size(), end, img.bytes.size());
        for (const auto& s : c.sections) {
            std::printf("section %d: flash $%06x, packed %u, sum %s, unpacked %zu\n", s.index, s.flashAddr, s.streamSize, s.sumOk ? "ok" : "BAD", s.data.size());
            if (!dumpDir.empty()) {
                const auto path = dumpDir + "/section" + std::to_string(s.index) + ".bin";
                if (FILE* f = std::fopen(path.c_str(), "wb")) { std::fwrite(s.data.data(), 1, s.data.size(), f); std::fclose(f); }
            }
            if (s.index == 0) continue;
            try {
                const auto d = parseDspRecords(s.data);
                size_t words[3][2] = {};
                for (const auto& r : d.records) words[int(r.space)][r.addr >= 0x100000] += r.words.size();
                std::printf("  DSP program: %zu records, start $%06x; P %zu+%zu, X %zu+%zu, Y %zu+%zu words (internal+external)\n",
                    d.records.size(), d.startAddr.value_or(0), words[0][0], words[0][1], words[1][0], words[1][1], words[2][0], words[2][1]);
                if (records)
                    for (const auto& r : d.records) std::printf("    %c:$%06x +%zu\n", "PXY"[int(r.space)], r.addr, r.words.size());
            } catch (const FirmwareError& e) {
                std::printf("  not DSP records (%s)\n", e.what());
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "mdfw: %s\n", e.what());
        return 1;
    }
    return 0;
}

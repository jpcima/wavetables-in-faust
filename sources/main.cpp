#include "sfizz/Wavetables.h"
#include "dr_wav_library.h"
#include <getopt.h>
#include <nonstd/scope.hpp>
#include <memory>
#include <cstdio>

struct Waveform {
    std::unique_ptr<float[]> data;
    uint32_t size = 0;
    explicit operator bool() const noexcept { return data != nullptr; }
};

static void show_usage();
static int read_file_waveform(const char *path, Waveform &wave);
static void write_mipmap(FILE *stream, sfz::WavetableMulti &mipmap);

int main(int argc, char *argv[])
{
    const char *input_path = nullptr;
    const char *output_path = nullptr;

    if (argc <= 1) {
        show_usage();
        return 0;
    }

    for (int c; (c = getopt(argc, argv, "hi:o:")) != -1;) {
        switch (c) {
        case 'h':
            show_usage();
            return 0;
        case 'i':
            input_path = optarg;
            break;
        case 'o':
            output_path = optarg;
            break;
        default:
            return 1;
        }
    }

    if (argc != optind || !input_path) {
        fprintf(stderr, "Invalid arguments\n");
        show_usage();
        return 1;
    }

    Waveform raw;
    int ret = read_file_waveform(input_path, raw);
    if (ret != 0)
        return ret;

    sfz::WavetableMulti mipmap = sfz::WavetableMulti::createFromAudioData(
        nonstd::span<const float>(raw.data.get(), raw.size), 1.0);

    ///
    FILE *output = stdout;
    if (output_path) {
        output = fopen(output_path, "wb");
        if (!output) {
            fprintf(stderr, "Cannot open output file.\n");
            return 1;
        }
    }

    write_mipmap(output, mipmap);

    if (output_path) {
        fflush(output);
        int err = ferror(output);
        fclose(output);
        if (err) {
            fprintf(stderr, "Cannot write output file.\n");
            return 1;
        }
    }

    return 0;
}

static void show_usage()
{
    fprintf(stderr, "Usage: make-wavetable-faust <-i wave-file> [-o output-file]\n");
}

static int read_file_waveform(const char *path, Waveform &wave)
{
    drwav wav_file;
    drwav_bool32 wav_init = drwav_init_file(&wav_file, path, nullptr);

    if (!wav_init) {
        fprintf(stderr, "Cannot open sound file.\n");
        return 1;
    }
    auto wav_file_cleanup = nonstd::make_scope_exit(
        [&wav_file]() { drwav_uninit(&wav_file); });

    if (wav_file.channels != 1) {
        fprintf(stderr, "Sound data does not contain exactly 1 channel.\n");
        return 1;
    }
    if (wav_file.totalPCMFrameCount > 65536) {
        fprintf(stderr, "Sound data is too large.\n");
        return 1;
    }
    if (wav_file.totalPCMFrameCount < 4) {
        fprintf(stderr, "Sound data is too small.\n");
        return 1;
    }
    if (wav_file.totalPCMFrameCount & 1) {
        fprintf(stderr, "Sound data must have an even size.\n");
        return 1;
    }

    wave.size = (uint32_t)wav_file.totalPCMFrameCount;
    wave.data.reset(new float[wave.size]);

    if (drwav_read_pcm_frames_f32(&wav_file, wave.size, wave.data.get()) != wave.size) {
        fprintf(stderr, "Cannot read sound data.\n");
        return 1;
    }

    return 0;
}

static void write_mipmap(FILE *stream, sfz::WavetableMulti &mipmap)
{
    uint32_t tableSize = mipmap.tableSize();
    fprintf(stream, "tableSize = %u;\n", tableSize);
    fprintf(stream, "numTables = %u;\n", sfz::MipmapRange::N);
    fprintf(stream, "firstStartFrequency = %f;\n", sfz::MipmapRange::F1);
    fprintf(stream, "lastStartFrequency = %f;\n", sfz::MipmapRange::FN);
    fprintf(stream, "waveData = waveform{\n");
    for (uint32_t tableNo = 0; tableNo < sfz::MipmapRange::N; ++tableNo) {
        const nonstd::span<const float> table = mipmap.getTable(tableNo);
        for (uint32_t i = 0; i < tableSize; ++i) {
            fprintf(stream, "%s%e", (i > 0) ? ", " : "  ", table[i]);
        }
        if (tableNo + 1 < sfz::MipmapRange::N)
            fprintf(stream, ",");
        fprintf(stream, "\n");
    }
    fprintf(stream, "} : (!, _);\n");
}

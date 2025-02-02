#include <iostream>
#include <cstring>
#include "iorg.h"
#include <zlib.h>

namespace iorg
{

// Variables.
bool flip_vertically_on_load = false;

// Implementations.
std::vector<uint8_t> load(const std::string &file_path, int &width, int &height)
{
    switch (getFormat(file_path))
    {
    case Format::BMP:
        return loadBMP(file_path, width, height);
    case Format::PNG:
        return loadPNG(file_path, width, height);
    case Format::JPEG:
        return loadJPEG(file_path, width, height);
    default:
        std::cerr << "[IORG] ERROR: Wrong file format for loading!"
                  << std::endl;
        return std::vector<uint8_t>();
    }
}

std::vector<uint8_t> loadBMP(const std::string &file_path, int &width,
                             int &height)
{
    std::ifstream file{file_path, std::ios_base::binary};
    if (!file)
    {
        std::cerr << "[IORG] ERROR: Failed to open an image file!" << std::endl;
        return std::vector<uint8_t>();
    }

    BMPHeader header;
    file.read(reinterpret_cast<char *>(&header), sizeof(header));
    if (header.file_type != 0x4D42)
    {
        std::cerr
            << "[IORG] ERROR: Loading BMP when trying to load other format!"
            << std::endl;
    }

    BMPInfoHeader info_header;
    file.read(reinterpret_cast<char *>(&info_header), sizeof(info_header));
    // TODO: Here should be a colour mask check.

    file.seekg(header.data_offset, file.beg);
    if (info_header.bits_per_pixel == 32)
    {
        info_header.header_size =
            sizeof(BMPInfoHeader) + sizeof(BMPColourHeader);
        header.data_offset =
            sizeof(BMPHeader) + sizeof(BMPInfoHeader) + sizeof(BMPColourHeader);
    }
    else
    {
        info_header.header_size = sizeof(BMPInfoHeader);
        header.data_offset = sizeof(BMPHeader) + sizeof(BMPInfoHeader);
    }

    header.file_size = header.data_offset;

    if (info_header.height < 0)
    {
        std::cout << "[IORG] WARNING: The program treats only BMP images with "
                     "the origin in the bottom left corner."
                  << std::endl;
    }

    width = info_header.width;
    height = info_header.height;

    int image_size = width * height * info_header.bits_per_pixel / 8;
    std::vector<uint8_t> pixel_data(image_size);

    if (info_header.width % 4 == 0)
    {
        file.read(reinterpret_cast<char *>(pixel_data.data()),
                  pixel_data.size());
        header.file_size += pixel_data.size();
    }
    else
    {
        uint32_t row_stride =
            info_header.width * info_header.bits_per_pixel / 8;
        uint32_t new_stride = makeStrideAligned(4, row_stride);

        std::vector<uint8_t> padding_row(new_stride - row_stride);

        for (int y = 0; y < info_header.height; ++y)
        {
            file.read(
                reinterpret_cast<char *>(pixel_data.data() + row_stride * y),
                row_stride);
            file.read(reinterpret_cast<char *>(padding_row.data()),
                      padding_row.size());
        }

        header.file_size +=
            pixel_data.size() + info_header.height * padding_row.size();
    }

    // Convert BGR(A) to RGB(A). We are assuming that BMP files are using
    // BGR(A)!
    size_t pixel_colour_offset = info_header.bits_per_pixel == 32 ? 4 : 3;
    for (size_t i = 0; i < pixel_data.size(); i += pixel_colour_offset)
    {
        uint8_t b = pixel_data[i];
        uint8_t g = pixel_data[i + 1];
        uint8_t r = pixel_data[i + 2];

        pixel_data[i] = r;
        pixel_data[i + 2] = b;
    }

    return pixel_data;
}

std::vector<uint8_t> loadPNG(const std::string &file_path, int &width,
                             int &height)
{
    std::ifstream file{file_path, std::ios_base::binary};
    if (!file)
    {
        std::cerr << "[IORG] ERROR: Failed to load image file!" << std::endl;
        return std::vector<uint8_t>();
    }

    const uint8_t png_signature[8] = {0x89, 0x50, 0x4E, 0x47,
                                      0x0D, 0x0A, 0x1A, 0x0A};
    uint8_t signature[8];
    file.read(reinterpret_cast<char *>(signature), 8);
    if (std::memcmp(signature, png_signature, 8) != 0)
    {
        std::cerr
            << "[IORG] ERROR: Loading PNG while trying to load other format!"
            << std::endl;
        return std::vector<uint8_t>();
    }

    PNGIHDR ihdr;
    std::vector<uint8_t> compressed_data;
    while (true)
    {
        PNGChunk chunk = readChunk(file);

        if (chunk.type == 0x49484452)
        {
            parseIHDR(chunk, ihdr);
        }
        else if (chunk.type == 0x49444154)
        {
            compressed_data.insert(compressed_data.end(), chunk.data.begin(),
                                   chunk.data.end());
        }
        else if (chunk.type == 0x49454E44)
        {
            break;
        }
    }

    width = ihdr.width;
    height = ihdr.height;

    std::vector<uint8_t> decompressed_data;
    if (!decompressIDAT(compressed_data, decompressed_data))
    {
        std::cerr << "[IORG] ERROR: Failed to decompress PNG data!"
                  << std::endl;
        return std::vector<uint8_t>();
    }

    for (int i = 0; i < ihdr.height; ++i)
    {
        applyFilter(decompressed_data, i, ihdr.width);
    }

    // Remove filter bytes from our data.
    std::vector<uint8_t> pixel_data;
    int row_counter = 0;
    for (size_t i = 1; i < decompressed_data.size(); ++i)
    {
        ++row_counter;
        if (row_counter == ihdr.width * 4 + 1)
        {
            row_counter = 0;
            continue;
        }

        pixel_data.push_back(decompressed_data[i]);
    }

    if (!flip_vertically_on_load)
    {
        return pixel_data;
    }

    // TODO: Think of a more compact solution.
    std::vector<uint8_t> flipped_pixel_data;
    for (int current_row = ihdr.height - 1; current_row >= 0; --current_row)
    {
        flipped_pixel_data.insert(flipped_pixel_data.end(),
                                  pixel_data.begin() + current_row * width * 4,
                                  pixel_data.begin() +
                                      (current_row + 1) * width * 4);
    }

    return flipped_pixel_data;
}

PNGChunk readChunk(std::ifstream &file)
{
    PNGChunk chunk;

    chunk.length = readUInt32(file);
    chunk.type = readUInt32(file);

    chunk.data.resize(chunk.length);
    file.read(reinterpret_cast<char *>(chunk.data.data()), chunk.length);

    file.read(reinterpret_cast<char *>(&chunk.crc), 4);

    return chunk;
}

void parseIHDR(const PNGChunk &chunk, PNGIHDR &ihdr)
{
    const uint8_t *data = chunk.data.data();

    ihdr.width =
        convertBigToNativeEndian(*reinterpret_cast<const uint32_t *>(data));
    ihdr.height =
        convertBigToNativeEndian(*reinterpret_cast<const uint32_t *>(data + 4));
    ihdr.bit_depth = data[8];
    ihdr.colour_type = data[9];
    ihdr.compression_method = data[10];
    ihdr.filter_method = data[11];
    ihdr.interlace_method = data[12];
}

bool decompressIDAT(const std::vector<uint8_t> &compressed_data,
                    std::vector<uint8_t> &decompressed_data)
{
    z_stream stream = {0};
    if (inflateInit(&stream) != Z_OK)
    {
        std::cerr << "[IORG] ERROR: zlib initialisation failed!" << std::endl;
        return false;
    }

    size_t decompressed_size = 1024 * 1024 * 1024;
    decompressed_data.resize(decompressed_size);

    stream.avail_in = compressed_data.size();
    stream.next_in = const_cast<Bytef *>(compressed_data.data());
    stream.avail_out = decompressed_size;
    stream.next_out = decompressed_data.data();

    int result = inflate(&stream, Z_NO_FLUSH);
    if (result != Z_OK && result != Z_STREAM_END)
    {
        std::cerr << "[IORG] ERROR: zlib decompression failed!" << std::endl;
        inflateEnd(&stream);
        return false;
    }

    decompressed_data.resize(decompressed_size - stream.avail_out);

    inflateEnd(&stream);
    return true;
}

void applyFilter(std::vector<uint8_t> &decompressed_data, int row_number,
                 int width)
{
    size_t row_start = row_number * (width * 4 + 1);
    uint8_t filter = decompressed_data[row_start];
    switch (filter)
    {
    case 0:
        break;
    case 1:
        // Using sub filter for the first pixel is useless, thus we apply it
        // from the second one.
        for (int i = 5; i < width * 4 + 1; ++i)
        {
            decompressed_data[row_start + i] +=
                decompressed_data[row_start + i - 4];
        }
        break;
    case 2:
        if (row_start == 0)
        {
            break;
        }

        for (int i = 1; i < width * 4 + 1; ++i)
        {
            decompressed_data[row_start + i] +=
                decompressed_data[row_start + i - (width * 4 + 1)];
        }
        break;
    case 3:
        for (int i = 1; i < width * 4 + 1; ++i)
        {
            uint8_t upper =
                row_start != 0
                    ? decompressed_data[row_start + i - (width * 4 + 1)]
                    : 0;
            uint8_t left = i >= 5 ? decompressed_data[row_start + i - 4] : 0;
            uint8_t average = (upper + left) / 2;
            decompressed_data[row_start + i] += average;
        }
        break;
    case 4:
        for (int i = 1; i < width * 4 + 1; ++i)
        {
            uint8_t upper =
                row_start != 0
                    ? decompressed_data[row_start + i - (width * 4 + 1)]
                    : 0;
            uint8_t left = i >= 5 ? decompressed_data[row_start + i - 4] : 0;
            uint8_t upper_left{0};
            if (row_start != 0 && i >= 5)
            {
                upper_left =
                    decompressed_data[row_start + i - 4 - (width * 4 + 1)];
            }

            decompressed_data[row_start + i] +=
                paethPredictor(left, upper, upper_left);
        }
        break;
    default:
        break;
    }
}

uint8_t paethPredictor(uint8_t left, uint8_t upper, uint8_t upper_left)
{
    int prediction = left + upper - upper_left;
    int left_distance = abs(prediction - left);
    int upper_distance = abs(prediction - upper);
    int upper_left_distance = abs(prediction - upper_left);

    if (left_distance <= upper_distance && left_distance <= upper_left_distance)
    {
        return left;
    }

    if (upper_distance <= upper_left_distance)
    {
        return upper;
    }

    return upper_left;
}

void setFlipVerticallyOnLoad(bool should_flip)
{
    flip_vertically_on_load = should_flip;
}

} // namespace iorg

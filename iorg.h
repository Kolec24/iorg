#include <fstream>
#include <vector>
#include <string>
#include <cstdint>

namespace iorg
{

// BMP.
#pragma pack(push, 1)

struct BMPHeader
{
    uint16_t file_type{0x4D42};
    uint32_t file_size{0};
    uint16_t reserved1{0};
    uint16_t reserved2{0};
    uint32_t data_offset{0};
};

#pragma pack(pop)

struct BMPInfoHeader
{
    uint32_t header_size{0};
    int32_t width{0};
    int32_t height{0};
    uint16_t colour_planes{1};
    uint16_t bits_per_pixel{0};
    uint32_t compression{0};
    uint32_t image_size{0};
    int32_t h_res{0};
    int32_t v_res{0};
    uint32_t colours_used{0};
    uint32_t colours_important{0};
};

struct BMPColourHeader
{
    uint32_t red_mask{0x00ff0000};
    uint32_t green_mask{0x0000ff00};
    uint32_t blue_mask{0x000000ff};
    uint32_t alpha_mask{0xff000000};
    uint32_t colour_space_type{0x73524742};
    uint32_t unused[16]{0};
};

// End BMP.

// PNG.
struct PNGIHDR
{
    uint32_t width{0};
    uint32_t height{0};
    uint8_t bit_depth{0};
    uint8_t colour_type{0};
    uint8_t compression_method{0};
    uint8_t filter_method{0};
    uint8_t interlace_method{0};
};

struct PNGChunk
{
    uint32_t length{0};
    uint32_t type{0};
    std::vector<uint8_t> data{0};
    uint32_t crc{0};
};

// End PNG.

enum class Format
{
    None,
    BMP,
    PNG,
    JPEG
};

std::vector<uint8_t> load(const std::string &file_path, int &width,
                          int &height);
std::vector<uint8_t> loadBMP(const std::string &file_path, int &width,
                             int &height);
std::vector<uint8_t> loadPNG(const std::string &file_path, int &width,
                             int &height);
inline std::vector<uint8_t> loadJPEG(const std::string &file_path, int &width,
                                     int &height)
{
    return std::vector<uint8_t>();
}

inline Format getFormat(const std::string &file_path)
{
    if (file_path.substr(file_path.length() - 4) == ".bmp")
        return Format::BMP;
    if (file_path.substr(file_path.length() - 4) == ".png")
        return Format::PNG;
    if (file_path.substr(file_path.length() - 4) == ".jpg" ||
        file_path.substr(file_path.length() - 5) == ".jpeg")
        return Format::JPEG;
    return Format::None;
}

inline uint32_t makeStrideAligned(uint32_t align_stride, uint32_t row_stride)
{
    uint32_t new_stride = row_stride;
    while (new_stride % align_stride != 0)
    {
        ++new_stride;
    }

    return new_stride;
}

inline bool isBigEndian(void)
{
    union
    {
        uint32_t i;
        char c[4];
    } bint = {0x01020304};

    return bint.c[0] = 1;
}

inline void uint32ToArray(uint32_t value, uint8_t (&data)[4])
{
    data[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    data[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[3] = static_cast<uint8_t>(value & 0xFF);
}

inline uint32_t convertBigToNativeEndian(uint32_t number)
{
    // TODO: Investigate how both actual and commented code performs on both little and big endian machines.
    // uint8_t data[4];
    // uint32ToArray(number, data);
    // return (data[3] << 0) | (data[2] << 8) | (data[1] << 16) | ((unsigned)data[0] << 24);
    return ((number >> 24) & 0x000000FF) |
           ((number >> 8)  & 0x0000FF00) |
           ((number << 8)  & 0x00FF0000) |
           ((number << 24) & 0xFF000000);
}

inline uint32_t readUInt32(std::ifstream& file)
{
    uint32_t value;
    file.read(reinterpret_cast<char*>(&value), sizeof(value));
    return convertBigToNativeEndian(value);
}

PNGChunk readChunk(std::ifstream& file);
void parseIHDR(const PNGChunk& chunk, PNGIHDR& ihdr);
bool decompressIDAT(const std::vector<uint8_t>& compressed_data, std::vector<uint8_t>& decompressed_data);
void applyFilter(std::vector<uint8_t>& decompressed_data, int row_number, int width);
uint8_t paethPredictor(uint8_t left, uint8_t upper, uint8_t upper_left);

void setFlipVerticallyOnLoad(bool should_flip);

}; // namespace iorg

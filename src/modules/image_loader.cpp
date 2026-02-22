#include "modules/image_loader.hpp"

#include <iostream>
#include <cstdio>
#include <cstdint>
#include <jpeglib.h>
#include <png.h>

namespace nuc_display::modules {

std::expected<void, MediaError> ImageLoader::load(const std::string& filepath) {
    if (filepath.ends_with(".jpg") || filepath.ends_with(".jpeg")) {
        return load_jpeg(filepath);
    } else if (filepath.ends_with(".png")) {
        return load_png(filepath);
    }
    
    std::cerr << "Unsupported image format: " << filepath << "\n";
    return std::unexpected(MediaError::UnsupportedFormat);
}

std::expected<void, MediaError> ImageLoader::process(double /*time_sec*/) {
    if (this->rgba_data_.empty()) {
        return std::unexpected(MediaError::InternalError);
    }
    return {};
}

std::expected<void, MediaError> ImageLoader::load_jpeg(const std::string& filepath) {
    FILE* file = std::fopen(filepath.c_str(), "rb");
    if (!file) {
        std::cerr << "Failed to open JPEG file: " << filepath << "\n";
        return std::unexpected(MediaError::FileNotFound);
    }

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, file);

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        std::fclose(file);
        jpeg_destroy_decompress(&cinfo);
        return std::unexpected(MediaError::DecodeFailed);
    }

    cinfo.out_color_space = JCS_EXT_RGBA;
    jpeg_start_decompress(&cinfo);

    this->width_ = cinfo.output_width;
    this->height_ = cinfo.output_height;
    this->channels_ = cinfo.output_components;

    this->rgba_data_.resize(this->width_ * this->height_ * this->channels_);
    
    while (cinfo.output_scanline < cinfo.output_height) {
        uint8_t* row_pointer = this->rgba_data_.data() + (cinfo.output_scanline * this->width_ * this->channels_);
        jpeg_read_scanlines(&cinfo, &row_pointer, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    std::fclose(file);

    std::cout << "Loaded JPEG: " << this->width_ << "x" << this->height_ << " (" << this->channels_ << " channels)\n";
    return {};
}

std::expected<void, MediaError> ImageLoader::load_png(const std::string& filepath) {
    FILE *file = std::fopen(filepath.c_str(), "rb");
    if (!file) {
        std::cerr << "Failed to open PNG file: " << filepath << "\n";
        return std::unexpected(MediaError::FileNotFound);
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        std::fclose(file);
        return std::unexpected(MediaError::InternalError);
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, nullptr, nullptr);
        std::fclose(file);
        return std::unexpected(MediaError::InternalError);
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(file);
        return std::unexpected(MediaError::DecodeFailed);
    }

    png_init_io(png, file);
    png_read_info(png, info);

    this->width_      = png_get_image_width(png, info);
    this->height_     = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth  = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);
    
    this->channels_ = 4;
    this->rgba_data_.resize(this->width_ * this->height_ * this->channels_);
    
    std::vector<png_bytep> row_pointers(this->height_);
    for (std::uint32_t y = 0; y < this->height_; y++) {
        row_pointers[y] = this->rgba_data_.data() + y * this->width_ * this->channels_;
    }

    png_read_image(png, row_pointers.data());
    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(file);

    std::cout << "Loaded PNG: " << this->width_ << "x" << this->height_ << " (" << this->channels_ << " channels)\n";
    return {};
}

} // namespace nuc_display::modules

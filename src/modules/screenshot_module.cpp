#include "screenshot_module.hpp"
#include <GLES2/gl2.h>
#include <png.h>
#include <cstdio>
#include <iostream>
#include <vector>

namespace nuc_display::modules {

std::expected<void, MediaError> ScreenshotModule::capture(int width, int height) {
    this->width_ = width;
    this->height_ = height;
    this->pixel_data_.resize(width * height * 4);

    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, this->pixel_data_.data());
    
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        std::cerr << "glReadPixels failed with error: " << err << "\n";
        return std::unexpected(MediaError::InternalError);
    }

    return {};
}

std::expected<void, MediaError> ScreenshotModule::save(const std::string& filepath) {
    if (this->pixel_data_.empty()) return std::unexpected(MediaError::InternalError);

    FILE* fp = std::fopen(filepath.c_str(), "wb");
    if (!fp) return std::unexpected(MediaError::FileNotFound);

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    if (!png) {
        std::fclose(fp);
        return std::unexpected(MediaError::InternalError);
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        std::fclose(fp);
        return std::unexpected(MediaError::InternalError);
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        std::fclose(fp);
        return std::unexpected(MediaError::InternalError);
    }

    png_init_io(png, fp);

    png_set_IHDR(
        png, info, this->width_, this->height_,
        8, PNG_COLOR_TYPE_RGBA, PNG_INTERLACE_NONE,
        PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT
    );

    png_write_info(png, info);

    // PNG is top-down, GL is bottom-up. Flip rows.
    std::vector<png_bytep> row_pointers(this->height_);
    for (int y = 0; y < this->height_; y++) {
        row_pointers[y] = (png_bytep)&this->pixel_data_[(this->height_ - 1 - y) * this->width_ * 4];
    }

    png_write_image(png, row_pointers.data());
    png_write_end(png, nullptr);

    png_destroy_write_struct(&png, &info);
    std::fclose(fp);

    std::cout << "Screenshot saved to: " << filepath << "\n";
    return {};
}

} // namespace nuc_display::modules

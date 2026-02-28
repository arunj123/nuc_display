#include "modules/camera_module.hpp"
#include "core/renderer.hpp"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <linux/videodev2.h>
#include <drm_fourcc.h>

// MJPEG → RGB decode via libjpeg (software fallback)
#include <jpeglib.h>

namespace nuc_display::modules {

// --- Helpers ---

static uint32_t pixel_format_from_string(const std::string& fmt) {
    if (fmt == "MJPG" || fmt == "mjpg") return V4L2_PIX_FMT_MJPEG;
    if (fmt == "YUYV" || fmt == "yuyv") return V4L2_PIX_FMT_YUYV;
    if (fmt == "NV12" || fmt == "nv12") return V4L2_PIX_FMT_NV12;
    if (fmt == "H264" || fmt == "h264") return V4L2_PIX_FMT_H264;
    return V4L2_PIX_FMT_MJPEG; // Default
}

static const char* fourcc_to_string(uint32_t fourcc) {
    static char buf[5];
    buf[0] = fourcc & 0xFF;
    buf[1] = (fourcc >> 8) & 0xFF;
    buf[2] = (fourcc >> 16) & 0xFF;
    buf[3] = (fourcc >> 24) & 0xFF;
    buf[4] = '\0';
    return buf;
}

static bool decode_mjpeg_to_rgb(const uint8_t* mjpeg_data, size_t mjpeg_size,
                                 uint8_t* rgb_out, int expected_w, int expected_h) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    
    jpeg_mem_src(&cinfo, mjpeg_data, mjpeg_size);
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    
    cinfo.out_color_space = JCS_RGB;
    jpeg_start_decompress(&cinfo);
    
    if ((int)cinfo.output_width != expected_w || (int)cinfo.output_height != expected_h) {
        jpeg_abort_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        return false;
    }
    
    int row_stride = cinfo.output_width * 3;
    while (cinfo.output_scanline < cinfo.output_height) {
        uint8_t* row = rgb_out + cinfo.output_scanline * row_stride;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    return true;
}

// --- CameraModule ---

CameraModule::CameraModule() {}

CameraModule::~CameraModule() {
    close();
}

bool CameraModule::is_capture_device(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK);
    if (fd < 0) return false;
    
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
        ::close(fd);
        return false;
    }
    
    bool is_capture = (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) &&
                      (cap.capabilities & V4L2_CAP_STREAMING);
    ::close(fd);
    return is_capture;
}

std::string CameraModule::find_camera_device() {
    DIR* dir = opendir("/sys/class/video4linux");
    if (!dir) return "";
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strncmp(entry->d_name, "video", 5) != 0) continue;
        
        // Read device name from sysfs
        std::string name_path = "/sys/class/video4linux/" + std::string(entry->d_name) + "/name";
        std::string dev_path = "/dev/" + std::string(entry->d_name);
        
        // Skip metadata devices (common on UVC cameras — they have a second /dev/videoN)
        std::ifstream name_file(name_path);
        if (name_file) {
            std::string name;
            std::getline(name_file, name);
            // Metadata interfaces often contain "Metadata" or "Meta" in name
            if (name.find("Metadata") != std::string::npos || 
                name.find("metadata") != std::string::npos) {
                continue;
            }
        }
        
        if (is_capture_device(dev_path)) {
            closedir(dir);
            std::cout << "[Camera] Auto-detected camera: " << dev_path << "\n";
            return dev_path;
        }
    }
    closedir(dir);
    return "";
}

bool CameraModule::open(const CameraConfig& config) {
    if (v4l2_fd_ >= 0) close();
    
    std::string device = config.device;
    if (device.empty()) {
        device = find_camera_device();
        if (device.empty()) {
            std::cerr << "[Camera] No camera device found for auto-detect.\n";
            return false;
        }
    }
    
    uint32_t fourcc = pixel_format_from_string(config.pixel_format);
    if (!init_v4l2(device, config.width, config.height, config.fps, fourcc)) {
        return false;
    }
    
    if (!start_streaming()) {
        cleanup_v4l2();
        return false;
    }
    
    std::cout << "[Camera] Opened " << device_name_ << " (" << device_path_ 
              << ") @ " << capture_width_ << "x" << capture_height_ 
              << " " << fourcc_to_string(capture_fourcc_)
              << (use_dmabuf_ ? " [DMA-BUF zero-copy]" : " [software upload]") << "\n";
    return true;
}

bool CameraModule::init_v4l2(const std::string& device, int w, int h, int fps, uint32_t fourcc) {
    v4l2_fd_ = ::open(device.c_str(), O_RDWR | O_NONBLOCK);
    if (v4l2_fd_ < 0) {
        std::cerr << "[Camera] Failed to open " << device << ": " << strerror(errno) << "\n";
        return false;
    }
    device_path_ = device;
    
    // Query device name
    struct v4l2_capability cap;
    memset(&cap, 0, sizeof(cap));
    if (ioctl(v4l2_fd_, VIDIOC_QUERYCAP, &cap) == 0) {
        device_name_ = reinterpret_cast<const char*>(cap.card);
    }
    
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) || !(cap.capabilities & V4L2_CAP_STREAMING)) {
        std::cerr << "[Camera] " << device << " does not support capture + streaming.\n";
        cleanup_v4l2();
        return false;
    }
    
    // Try to set the requested format exactly as specified in the config.
    // We shouldn't silently fallback to NV12/YUYV if the user requested MJPEG, 
    // because USB webcams often lack bandwidth for uncompressed YUYV at high resolutions 
    // leading to "cannot set freq at ep" USB errors.
    
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = w;
    fmt.fmt.pix.height = h;
    fmt.fmt.pix.pixelformat = fourcc;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    
    if (ioctl(v4l2_fd_, VIDIOC_S_FMT, &fmt) == 0) {
        capture_fourcc_ = fmt.fmt.pix.pixelformat;
        capture_width_ = fmt.fmt.pix.width;
        capture_height_ = fmt.fmt.pix.height;
    } else {
        std::cerr << "[Camera] Failed to set format " << fourcc_to_string(fourcc) 
                  << " at " << w << "x" << h << " on " << device << "\n";
        cleanup_v4l2();
        return false;
    }
    
    // Set frame rate
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = fps;
    ioctl(v4l2_fd_, VIDIOC_S_PARM, &parm); // Best-effort, not all cameras support this
    
    // Request buffers (MMAP)
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = NUM_BUFFERS;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(v4l2_fd_, VIDIOC_REQBUFS, &req) < 0) {
        std::cerr << "[Camera] VIDIOC_REQBUFS failed: " << strerror(errno) << "\n";
        cleanup_v4l2();
        return false;
    }
    
    buffers_.resize(req.count);
    
    // Map buffers and try EXPBUF for DMA-BUF export
    use_dmabuf_ = true;
    for (unsigned int i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(v4l2_fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            std::cerr << "[Camera] VIDIOC_QUERYBUF failed for buffer " << i << "\n";
            cleanup_v4l2();
            return false;
        }
        
        buffers_[i].length = buf.length;
        buffers_[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, v4l2_fd_, buf.m.offset);
        
        if (buffers_[i].start == MAP_FAILED) {
            std::cerr << "[Camera] mmap failed for buffer " << i << "\n";
            buffers_[i].start = nullptr;
            cleanup_v4l2();
            return false;
        }
        
        // Try to export as DMA-BUF
        struct v4l2_exportbuffer expbuf;
        memset(&expbuf, 0, sizeof(expbuf));
        expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        expbuf.index = i;
        expbuf.flags = O_RDONLY;
        
        if (ioctl(v4l2_fd_, VIDIOC_EXPBUF, &expbuf) == 0) {
            buffers_[i].dmabuf_fd = expbuf.fd;
        } else {
            use_dmabuf_ = false;
            buffers_[i].dmabuf_fd = -1;
        }
    }
    
    // If format is MJPEG, we can't use DMA-BUF for direct EGL import
    // (EGL expects raw pixel data like NV12/YUYV, not compressed JPEG)
    if (capture_fourcc_ == V4L2_PIX_FMT_MJPEG || capture_fourcc_ == V4L2_PIX_FMT_JPEG) {
        use_dmabuf_ = false;
        sw_upload_ = true;
        rgb_buffer_.resize(capture_width_ * capture_height_ * 3);
    } else if (!use_dmabuf_) {
        sw_upload_ = true;
        // For YUYV/NV12 without DMA-BUF, we'd need to convert to RGB
        // For simplicity, we try to use the raw buffer with glTexImage2D
        rgb_buffer_.resize(capture_width_ * capture_height_ * 3);
    }
    
    return true;
}

bool CameraModule::start_streaming() {
    // Queue all buffers
    for (unsigned int i = 0; i < buffers_.size(); ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(v4l2_fd_, VIDIOC_QBUF, &buf) < 0) {
            std::cerr << "[Camera] VIDIOC_QBUF failed: " << strerror(errno) << "\n";
            return false;
        }
    }
    
    // Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(v4l2_fd_, VIDIOC_STREAMON, &type) < 0) {
        std::cerr << "[Camera] VIDIOC_STREAMON failed: " << strerror(errno) << "\n";
        return false;
    }
    
    streaming_ = true;
    return true;
}

void CameraModule::stop_streaming() {
    if (streaming_ && v4l2_fd_ >= 0) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(v4l2_fd_, VIDIOC_STREAMOFF, &type);
        streaming_ = false;
    }
}

void CameraModule::cleanup_v4l2() {
    stop_streaming();
    
    for (auto& buf : buffers_) {
        if (buf.start && buf.start != MAP_FAILED) {
            munmap(buf.start, buf.length);
        }
        if (buf.dmabuf_fd >= 0) {
            ::close(buf.dmabuf_fd);
        }
    }
    buffers_.clear();
    
    if (v4l2_fd_ >= 0) {
        ::close(v4l2_fd_);
        v4l2_fd_ = -1;
    }
    
    // Cleanup GL resources
    if (current_egl_image_ != EGL_NO_IMAGE_KHR && eglDestroyImageKHR_) {
        eglDestroyImageKHR_(egl_display_, current_egl_image_);
        current_egl_image_ = EGL_NO_IMAGE_KHR;
    }
    if (texture_id_ != 0) {
        glDeleteTextures(1, &texture_id_);
        texture_id_ = 0;
    }
    if (sw_texture_id_ != 0) {
        glDeleteTextures(1, &sw_texture_id_);
        sw_texture_id_ = 0;
    }
    if (program_ != 0) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    
    use_dmabuf_ = false;
    sw_upload_ = false;
    has_frame_ = false;
    current_buf_index_ = -1;
    gl_initialized_ = false;
    device_path_.clear();
    device_name_.clear();
    capture_fourcc_ = 0;
    capture_width_ = 0;
    capture_height_ = 0;
    rgb_buffer_.clear();
}

void CameraModule::close() {
    cleanup_v4l2();
}

bool CameraModule::is_open() const {
    return v4l2_fd_ >= 0 && streaming_;
}

bool CameraModule::capture_frame() {
    if (v4l2_fd_ < 0 || !streaming_) return false;
    
    // Non-blocking poll
    struct pollfd pfd;
    pfd.fd = v4l2_fd_;
    pfd.events = POLLIN;
    pfd.revents = 0;
    
    int ret = poll(&pfd, 1, 0); // Non-blocking
    if (ret < 0) {
        std::cerr << "[Camera] poll() error: " << strerror(errno) << "\n";
        return false;
    }
    
    if (pfd.revents & (POLLERR | POLLHUP)) {
        std::cerr << "[Camera] Device disconnected (" << device_path_ << ")\n";
        return false;
    }
    
    if (!(pfd.revents & POLLIN)) {
        return true; // No new frame, but camera is still connected
    }
    
    // Re-queue the previous buffer if one was dequeued (for DMA-BUF path)
    if (current_buf_index_ >= 0) {
        struct v4l2_buffer qbuf;
        memset(&qbuf, 0, sizeof(qbuf));
        qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        qbuf.memory = V4L2_MEMORY_MMAP;
        qbuf.index = current_buf_index_;
        ioctl(v4l2_fd_, VIDIOC_QBUF, &qbuf);
        current_buf_index_ = -1;
    }
    
    // Dequeue the new buffer
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    
    if (ioctl(v4l2_fd_, VIDIOC_DQBUF, &buf) < 0) {
        if (errno == EAGAIN) return true; // No frame ready
        if (errno == EIO) {
            std::cerr << "[Camera] I/O error on " << device_path_ << " — camera likely disconnected.\n";
            return false;
        }
        std::cerr << "[Camera] VIDIOC_DQBUF error: " << strerror(errno) << "\n";
        return false;
    }
    
    if (sw_upload_) {
        // Software path: decode MJPEG or convert YUYV to RGB
        if (capture_fourcc_ == V4L2_PIX_FMT_MJPEG || capture_fourcc_ == V4L2_PIX_FMT_JPEG) {
            decode_mjpeg_to_rgb(
                static_cast<uint8_t*>(buffers_[buf.index].start), buf.bytesused,
                rgb_buffer_.data(), capture_width_, capture_height_
            );
        } else if (capture_fourcc_ == V4L2_PIX_FMT_YUYV) {
            // YUYV → RGB conversion
            const uint8_t* src = static_cast<uint8_t*>(buffers_[buf.index].start);
            uint8_t* dst = rgb_buffer_.data();
            int pixels = capture_width_ * capture_height_;
            for (int i = 0; i < pixels; i += 2) {
                int y0 = src[0]; int u = src[1]; int y1 = src[2]; int v = src[3];
                src += 4;
                int c0 = y0 - 16, c1 = y1 - 16, d = u - 128, e = v - 128;
                dst[0] = std::clamp((298 * c0 + 409 * e + 128) >> 8, 0, 255);
                dst[1] = std::clamp((298 * c0 - 100 * d - 208 * e + 128) >> 8, 0, 255);
                dst[2] = std::clamp((298 * c0 + 516 * d + 128) >> 8, 0, 255);
                dst[3] = std::clamp((298 * c1 + 409 * e + 128) >> 8, 0, 255);
                dst[4] = std::clamp((298 * c1 - 100 * d - 208 * e + 128) >> 8, 0, 255);
                dst[5] = std::clamp((298 * c1 + 516 * d + 128) >> 8, 0, 255);
                dst += 6;
            }
        }
        
        // Re-queue immediately for software path
        struct v4l2_buffer qbuf;
        memset(&qbuf, 0, sizeof(qbuf));
        qbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        qbuf.memory = V4L2_MEMORY_MMAP;
        qbuf.index = buf.index;
        ioctl(v4l2_fd_, VIDIOC_QBUF, &qbuf);
    } else {
        // DMA-BUF path: keep buffer dequeued until next frame
        current_buf_index_ = buf.index;
    }
    
    has_frame_ = true;
    return true;
}

void CameraModule::init_gl(core::Renderer& renderer, EGLDisplay egl_display) {
    egl_display_ = egl_display;
    
    // Cache EGL function pointers
    eglCreateImageKHR_ = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR_ = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    glEGLImageTargetTexture2DOES_ = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    
    if (use_dmabuf_ && eglCreateImageKHR_ && glEGLImageTargetTexture2DOES_) {
        // External OES shader (same as VideoDecoder — hardware YUV→RGB)
        const char* vs = R"(
            attribute vec4 a_position;
            attribute vec2 a_texCoord;
            varying vec2 v_texCoord;
            void main() {
                gl_Position = a_position;
                v_texCoord = a_texCoord;
            }
        )";
        const char* fs = R"(
            #extension GL_OES_EGL_image_external : require
            precision mediump float;
            varying vec2 v_texCoord;
            uniform samplerExternalOES s_texture;
            void main() {
                gl_FragColor = texture2D(s_texture, v_texCoord);
            }
        )";
        
        GLuint vs_id = renderer.compile_shader(GL_VERTEX_SHADER, vs);
        GLuint fs_id = renderer.compile_shader(GL_FRAGMENT_SHADER, fs);
        program_ = renderer.link_program(vs_id, fs_id);
        glDeleteShader(vs_id);
        glDeleteShader(fs_id);
        
        glGenTextures(1, &texture_id_);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture_id_);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        // Software path: standard 2D texture shader
        const char* vs = R"(
            attribute vec4 a_position;
            attribute vec2 a_texCoord;
            varying vec2 v_texCoord;
            void main() {
                gl_Position = a_position;
                v_texCoord = a_texCoord;
            }
        )";
        const char* fs = R"(
            precision mediump float;
            varying vec2 v_texCoord;
            uniform sampler2D s_texture;
            void main() {
                gl_FragColor = texture2D(s_texture, v_texCoord);
            }
        )";
        
        GLuint vs_id = renderer.compile_shader(GL_VERTEX_SHADER, vs);
        GLuint fs_id = renderer.compile_shader(GL_FRAGMENT_SHADER, fs);
        program_ = renderer.link_program(vs_id, fs_id);
        glDeleteShader(vs_id);
        glDeleteShader(fs_id);
        
        glGenTextures(1, &sw_texture_id_);
        glBindTexture(GL_TEXTURE_2D, sw_texture_id_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    
    pos_loc_ = glGetAttribLocation(program_, "a_position");
    tex_coord_loc_ = glGetAttribLocation(program_, "a_texCoord");
    sampler_loc_ = glGetUniformLocation(program_, "s_texture");
    
    gl_initialized_ = true;
}

void CameraModule::render(core::Renderer& renderer, EGLDisplay egl_display,
                          float src_x, float src_y, float src_w, float src_h,
                          float x, float y, float w, float h) {
    if (!has_frame_) return;
    
    if (!gl_initialized_) {
        init_gl(renderer, egl_display);
    }
    
    // Update texture from current frame
    if (use_dmabuf_ && current_buf_index_ >= 0) {
        // DMA-BUF → EGLImage → external OES texture
        if (current_egl_image_ != EGL_NO_IMAGE_KHR) {
            eglDestroyImageKHR_(egl_display_, current_egl_image_);
            current_egl_image_ = EGL_NO_IMAGE_KHR;
        }
        
        // Determine DRM fourcc from V4L2 fourcc
        uint32_t drm_fourcc = DRM_FORMAT_NV12; // default
        if (capture_fourcc_ == V4L2_PIX_FMT_NV12) drm_fourcc = DRM_FORMAT_NV12;
        else if (capture_fourcc_ == V4L2_PIX_FMT_YUYV) drm_fourcc = DRM_FORMAT_YUYV;
        
        EGLint attribs[] = {
            EGL_WIDTH, capture_width_,
            EGL_HEIGHT, capture_height_,
            EGL_LINUX_DRM_FOURCC_EXT, (EGLint)drm_fourcc,
            EGL_DMA_BUF_PLANE0_FD_EXT, buffers_[current_buf_index_].dmabuf_fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, capture_width_ * ((drm_fourcc == DRM_FORMAT_YUYV) ? 2 : 1),
            EGL_NONE
        };
        
        // For NV12, add UV plane
        if (drm_fourcc == DRM_FORMAT_NV12) {
            // NV12: Y plane pitch = width, UV plane offset = width*height, pitch = width
            std::vector<EGLint> nv12_attribs = {
                EGL_WIDTH, capture_width_,
                EGL_HEIGHT, capture_height_,
                EGL_LINUX_DRM_FOURCC_EXT, (EGLint)DRM_FORMAT_NV12,
                EGL_DMA_BUF_PLANE0_FD_EXT, buffers_[current_buf_index_].dmabuf_fd,
                EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
                EGL_DMA_BUF_PLANE0_PITCH_EXT, capture_width_,
                EGL_DMA_BUF_PLANE1_FD_EXT, buffers_[current_buf_index_].dmabuf_fd,
                EGL_DMA_BUF_PLANE1_OFFSET_EXT, capture_width_ * capture_height_,
                EGL_DMA_BUF_PLANE1_PITCH_EXT, capture_width_,
                EGL_NONE
            };
            
            current_egl_image_ = eglCreateImageKHR_(
                egl_display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, nv12_attribs.data()
            );
        } else {
            current_egl_image_ = eglCreateImageKHR_(
                egl_display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attribs
            );
        }
        
        if (current_egl_image_ != EGL_NO_IMAGE_KHR) {
            glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture_id_);
            glEGLImageTargetTexture2DOES_(GL_TEXTURE_EXTERNAL_OES, current_egl_image_);
        } else {
            return; // Can't render without a valid EGLImage
        }
    } else if (sw_upload_ && !rgb_buffer_.empty()) {
        // Software path: upload RGB data
        glBindTexture(GL_TEXTURE_2D, sw_texture_id_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, capture_width_, capture_height_, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, rgb_buffer_.data());
    } else {
        return; // No frame data
    }
    
    // Draw the texture quad (same rendering pattern as VideoDecoder)
    glUseProgram(program_);
    
    float nx = x * 2.0f - 1.0f;
    float ny = 1.0f - y * 2.0f;
    float nw = w * 2.0f;
    float nh = h * 2.0f;
    
    float vertices[] = {
        nx,      ny - nh, src_x,         src_y + src_h,
        nx + nw, ny - nh, src_x + src_w, src_y + src_h,
        nx,      ny,      src_x,         src_y,
        nx + nw, ny,      src_x + src_w, src_y,
    };
    
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    glVertexAttribPointer(pos_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &vertices[0]);
    glEnableVertexAttribArray(pos_loc_);
    
    glVertexAttribPointer(tex_coord_loc_, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), &vertices[2]);
    glEnableVertexAttribArray(tex_coord_loc_);
    
    glActiveTexture(GL_TEXTURE2); // Use unit 2 to avoid conflict with video (unit 1) and UI (unit 0)
    if (use_dmabuf_) {
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture_id_);
    } else {
        glBindTexture(GL_TEXTURE_2D, sw_texture_id_);
    }
    glUniform1i(sampler_loc_, 2);
    
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    
    glDisableVertexAttribArray(pos_loc_);
    glDisableVertexAttribArray(tex_coord_loc_);
    
    // Cleanup GL state
    glActiveTexture(GL_TEXTURE2);
    if (use_dmabuf_) {
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
    } else {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo());
}

} // namespace nuc_display::modules

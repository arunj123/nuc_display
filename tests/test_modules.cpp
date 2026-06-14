#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <curl/curl.h>
#include "modules/image_loader.hpp"
#include "modules/text_renderer.hpp"

// Global test environment: ensures curl is initialized before any module tests
class CurlEnvironment : public ::testing::Environment {
public:
    void SetUp() override { curl_global_init(CURL_GLOBAL_ALL); }
    void TearDown() override { curl_global_cleanup(); }
};
static auto* const curl_env = ::testing::AddGlobalTestEnvironment(new CurlEnvironment);

using namespace nuc_display::modules;

#include <fstream>

TEST(ImageLoaderTest, UnsupportedFormat) {
    std::ofstream out("dummy.txt");
    out << "Not an image";
    out.close();

    ImageLoader loader;
    auto result = loader.load("dummy.txt");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), MediaError::UnsupportedFormat);
    
    std::remove("dummy.txt");
}

TEST(ImageLoaderTest, FileNotFound) {
    ImageLoader loader;
    auto result = loader.load("nonexistent_image.jpg");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), MediaError::FileNotFound);
}

TEST(TextRendererTest, Initialization) {
    // This might fail if FreeType isn't available in the environment,
    // but the constructor should at least run.
    TextRenderer renderer;
}

TEST(TextRendererTest, ShapeEmptyText) {
    TextRenderer renderer;
    // Note: This requires a font to be loaded to actually work.
    // So we just check that it handles missing font gracefully.
    auto result = renderer.shape_text("Hello");
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), MediaError::InternalError);
}

#include "modules/weather_module.hpp"

TEST(WeatherModuleTest, DescriptionAndIconMapping) {
    WeatherModule module;
    EXPECT_EQ(module.get_weather_description(0), "Clear sky");
    EXPECT_EQ(module.get_weather_icon_filename(0), "assets/weather/clear.png");
    
    // Check Storms
    EXPECT_EQ(module.get_weather_description(95), "Thunderstorm: Slight or moderate");
    EXPECT_EQ(module.get_weather_icon_filename(95), "assets/weather/storm.png");
    
    // Check Rain
    EXPECT_EQ(module.get_weather_description(65), "Rain: Slight, moderate and heavy intensity");
    EXPECT_EQ(module.get_weather_icon_filename(65), "assets/weather/rain.png");
    
    // Check Snow
    EXPECT_EQ(module.get_weather_description(71), "Snow fall: Slight, moderate, and heavy intensity");
    EXPECT_EQ(module.get_weather_icon_filename(71), "assets/weather/snow.png");

    EXPECT_EQ(module.get_weather_description(999), "Unknown");
    EXPECT_EQ(module.get_weather_icon_filename(999), "assets/weather/unknown.png");
}

#include "modules/stock_module.hpp"

TEST(StockModuleTest, InvalidSymbolHandled) {
    StockModule module;
    // Test that fetching/parsing an invalid symbol gracefully fails and doesn't crash
    module.add_symbol("INVALID_SYMBOL_ABC_123", "Invalid");
    module.update_all_data();
    
    // Check that we don't have crash and state is still valid
    SUCCEED();
}

#include "modules/config_module.hpp"
#include <nlohmann/json.hpp>

class ConfigModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_file_ = "test_config_" + std::to_string(std::rand()) + ".json";
    }

    void TearDown() override {
        std::remove(test_file_.c_str());
    }

    std::string test_file_;
};

TEST_F(ConfigModuleTest, CreateDefaultWhenFileMissing) {
    ConfigModule config;
    auto result = config.load_or_create_config(test_file_);
    ASSERT_TRUE(result.has_value());
    
    // Video config defaults Check
    ASSERT_FALSE(result->videos.empty());
    EXPECT_TRUE(result->videos[0].enabled);
    EXPECT_FALSE(result->videos[0].audio_enabled);
    EXPECT_EQ(result->videos[0].playlists[0], "tests/sample.mp4");
    EXPECT_FLOAT_EQ(result->videos[0].x, 0.70f);
    EXPECT_FLOAT_EQ(result->videos[0].src_w, 1.0f);
}

TEST_F(ConfigModuleTest, ParseValidConfig) {
    nlohmann::json j = {
        {"location", {{"name", "London"}, {"lat", 51.5}, {"lon", -0.1}}},
        {"stocks", {{{"symbol", "AAPL"}, {"name", "Apple"}, {"currency_symbol", "$"}},
                    {{"symbol", "GOOG"}, {"name", "Alphabet"}, {"currency_symbol", "$"}}}},
        {"video", {
            {"enabled", false},
            {"audio_enabled", true},
            {"playlists", {"custom_video1.mp4", "custom_video2.mp4"}},
            {"x", 0.1f}, {"y", 0.2f}, {"w", 0.3f}, {"h", 0.4f},
            {"src_x", 0.1f}, {"src_y", 0.1f}, {"src_w", 0.8f}, {"src_h", 0.8f}
        }}
    };
    std::ofstream out(test_file_);
    out << j.dump();
    out.close();

    ConfigModule config;
    auto result = config.load_or_create_config(test_file_);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->location.name, "London");
    EXPECT_EQ(result->stocks.size(), 2);
    ASSERT_FALSE(result->videos.empty());
    EXPECT_FALSE(result->videos[0].enabled);
    EXPECT_TRUE(result->videos[0].audio_enabled);
    EXPECT_EQ(result->videos[0].playlists.size(), 2);
    EXPECT_EQ(result->videos[0].playlists[0], "custom_video1.mp4");
    EXPECT_EQ(result->videos[0].playlists[1], "custom_video2.mp4");
    EXPECT_FLOAT_EQ(result->videos[0].x, 0.1f);
    EXPECT_FLOAT_EQ(result->videos[0].src_x, 0.1f);
    EXPECT_FLOAT_EQ(result->videos[0].src_w, 0.8f);
}

TEST_F(ConfigModuleTest, HandleCorruptedJson) {
    std::ofstream out(test_file_);
    out << "{ invalid_json: ";
    out.close();

    ConfigModule config;
    auto result = config.load_or_create_config(test_file_);
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), ConfigError::ParseError);
}

TEST_F(ConfigModuleTest, HandleMissingVideoNode) {
    nlohmann::json j = {
        {"location", {{"name", "London"}, {"lat", 51.5}, {"lon", -0.1}}},
        {"stocks", nlohmann::json::array()}
    };
    std::ofstream out(test_file_);
    out << j.dump();
    out.close();

    ConfigModule config;
    auto result = config.load_or_create_config(test_file_);
    ASSERT_TRUE(result.has_value());
    // Should fallback to default video config and save
    ASSERT_FALSE(result->videos.empty());
    EXPECT_TRUE(result->videos[0].enabled);
    EXPECT_EQ(result->videos[0].playlists[0], "tests/sample.mp4");
}

#include "modules/config_validator.hpp"

TEST(ConfigValidatorTest, ValidConfigPasses) {
    AppConfig config;
    config.location = {"Test", 0.0f, 0.0f};
    config.stocks.push_back({"AAPL", "Apple", "$"});
    config.global_keys.hide_videos = 35; // KEY_H
    
    VideoConfig v1;
    v1.enabled = true;
    v1.playlists = {"file.mp4"};
    v1.start_trigger_key = 16; // KEY_Q
    v1.keys.next = 106; // KEY_RIGHT
    config.videos.push_back(v1);

    auto errors = ConfigValidator::validate(config);
    EXPECT_TRUE(errors.empty());
}

TEST(ConfigValidatorTest, DuplicateKeyDetected) {
    AppConfig config;
    config.location = {"Test", 0.0f, 0.0f};
    config.stocks.push_back({"AAPL", "Apple", "$"});
    config.global_keys.hide_videos = 35; // KEY_H
    
    VideoConfig v1;
    v1.enabled = true;
    v1.playlists = {"file.mp4"};
    v1.start_trigger_key = 35; // Duplicate with global_keys!
    config.videos.push_back(v1);

    auto errors = ConfigValidator::validate(config);
    ASSERT_EQ(errors.size(), 1);
    EXPECT_NE(errors[0].find("Duplicate key binding"), std::string::npos);
}

TEST(ConfigValidatorTest, MissingPlaylist) {
    AppConfig config;
    config.location = {"Test", 0.0f, 0.0f};
    config.stocks.push_back({"AAPL", "Apple", "$"});
    
    VideoConfig v1;
    v1.enabled = true;
    v1.playlists.clear(); // Empty playlist!
    config.videos.push_back(v1);

    auto errors = ConfigValidator::validate(config);
    ASSERT_EQ(errors.size(), 1);
    EXPECT_NE(errors[0].find("has no playlists"), std::string::npos);
}

TEST(ConfigValidatorTest, OutOfRangeCoordinates) {
    AppConfig config;
    config.location = {"Test", 0.0f, 0.0f};
    config.stocks.push_back({"AAPL", "Apple", "$"});
    
    VideoConfig v1;
    v1.enabled = true;
    v1.playlists = {"file.mp4"};
    v1.x = 1.5f; // Invalid!
    config.videos.push_back(v1);

    auto errors = ConfigValidator::validate(config);
    ASSERT_EQ(errors.size(), 1);
    EXPECT_NE(errors[0].find("out of range"), std::string::npos);
}

// --- Stock Key Binding Tests ---

TEST_F(ConfigModuleTest, ParseStockKeys) {
    nlohmann::json j = {
        {"location", {{"name", "London"}, {"lat", 51.5}, {"lon", -0.1}}},
        {"stocks", {{{"symbol", "AAPL"}, {"name", "Apple"}, {"currency_symbol", "$"}}}},
        {"stock_keys", {
            {"next_stock", "dot"},
            {"prev_stock", "comma"},
            {"next_chart", "equal"},
            {"prev_chart", "minus"}
        }},
        {"videos", nlohmann::json::array()}
    };
    std::ofstream out(test_file_);
    out << j.dump();
    out.close();

    ConfigModule config;
    auto result = config.load_or_create_config(test_file_);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->stock_keys.next_stock.has_value());
    EXPECT_TRUE(result->stock_keys.prev_stock.has_value());
    EXPECT_TRUE(result->stock_keys.next_chart.has_value());
    EXPECT_TRUE(result->stock_keys.prev_chart.has_value());
    // All four keys should be distinct
    EXPECT_NE(*result->stock_keys.next_stock, *result->stock_keys.prev_stock);
    EXPECT_NE(*result->stock_keys.next_chart, *result->stock_keys.prev_chart);
}

TEST_F(ConfigModuleTest, ParsePowerSaveConfig) {
    nlohmann::json j = {
        {"location", {{"name", "London"}, {"lat", 51.5}, {"lon", -0.1}}},
        {"stocks", nlohmann::json::array()},
        {"videos", nlohmann::json::array()},
        {"power_save", {
            {"enabled", true},
            {"start_time", "22:15"},
            {"end_time", "06:45"}
        }}
    };
    std::ofstream out(test_file_);
    out << j.dump();
    out.close();

    ConfigModule config;
    auto result = config.load_or_create_config(test_file_);
    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->power_save.enabled);
    EXPECT_EQ(result->power_save.start_time, "22:15");
    EXPECT_EQ(result->power_save.end_time, "06:45");
}


TEST(ConfigValidatorTest, StockKeyDuplicateWithGlobal) {
    AppConfig config;
    config.location = {"Test", 0.0f, 0.0f};
    config.stocks.push_back({"AAPL", "Apple", "$"});
    config.global_keys.hide_videos = 47; // KEY_V
    config.stock_keys.next_stock = 47;   // Duplicate with global!

    auto errors = ConfigValidator::validate(config);
    ASSERT_GE(errors.size(), 1u);
    EXPECT_NE(errors[0].find("Duplicate key binding"), std::string::npos);
}

TEST(ConfigValidatorTest, StockKeyDuplicateAmongStockKeys) {
    AppConfig config;
    config.location = {"Test", 0.0f, 0.0f};
    config.stocks.push_back({"AAPL", "Apple", "$"});
    config.stock_keys.next_stock = 52;  // KEY_DOT
    config.stock_keys.prev_stock = 52;  // Same as next_stock!

    auto errors = ConfigValidator::validate(config);
    ASSERT_GE(errors.size(), 1u);
    EXPECT_NE(errors[0].find("Duplicate key binding"), std::string::npos);
}

TEST(ConfigValidatorTest, PowerSaveInvalidTime) {
    AppConfig config;
    config.location = {"Test", 0.0f, 0.0f};
    config.stocks.push_back({"AAPL", "Apple", "$"});
    config.power_save.enabled = true;
    config.power_save.start_time = "25:00";
    config.power_save.end_time = "07:99";

    auto errors = ConfigValidator::validate(config);
    ASSERT_GE(errors.size(), 2u);
    EXPECT_NE(errors[0].find("contains invalid time"), std::string::npos);
    EXPECT_NE(errors[1].find("contains invalid time"), std::string::npos);

    config.power_save.start_time = "23:0";
    errors = ConfigValidator::validate(config);
    ASSERT_GE(errors.size(), 1u);
    EXPECT_NE(errors[0].find("must be in 'HH:MM' format"), std::string::npos);
}

TEST(StockModuleTest, ManualCyclingLogic) {
    StockModule module;
    
    std::vector<StockData> test_data;
    test_data.push_back({"AAPL", "Apple", "$", 150.0f, {}});
    test_data.push_back({"GOOG", "Alphabet", "$", 2800.0f, {}});
    module.clear_and_inject_test_data(test_data);

    EXPECT_FALSE(module.is_manual_mode());
    EXPECT_EQ(module.get_current_index(), 0u);

    module.next_stock();
    EXPECT_TRUE(module.is_manual_mode());
    EXPECT_EQ(module.get_current_index(), 1u);
    
    module.next_chart();
    EXPECT_TRUE(module.is_manual_mode());
    
    module.prev_stock();
    EXPECT_EQ(module.get_current_index(), 0u);
}

TEST(StockModuleTest, ManualModeAutoReset) {
    StockModule module;
    // We don't need stock_data to be non-empty now since we moved the logic to the top of render().
    
    nuc_display::core::Renderer renderer;
    nuc_display::modules::TextRenderer text_renderer;

    module.next_stock();
    EXPECT_TRUE(module.is_manual_mode());
    
    // Render at t=0 (Initializes manual_start_time_)
    module.render(renderer, text_renderer, 0.0);
    EXPECT_TRUE(module.is_manual_mode());
    
    // Render at t=10 (Within 15s timeout)
    module.render(renderer, text_renderer, 10.0);
    EXPECT_TRUE(module.is_manual_mode());
    
    // Render at t=16 (Exceeds 15s timeout)
    module.render(renderer, text_renderer, 16.0);
    
    EXPECT_FALSE(module.is_manual_mode());
}

TEST_F(ConfigModuleTest, ParseLayoutOrder) {
    nlohmann::json j = {
        {"location", {{"name", "London"}, {"lat", 51.5}, {"lon", -0.1}}},
        {"stocks", nlohmann::json::array()},
        {"videos", nlohmann::json::array()},
        {"layout", {
            {{"type", "stocks"}},
            {{"type", "video"}, {"video_index", 2}},
            {{"type", "weather"}},
            {{"type", "news"}},
            {{"type", "video"}, {"video_index", 0}}
        }}
    };
    std::ofstream out(test_file_);
    out << j.dump();
    out.close();

    ConfigModule config;
    auto result = config.load_or_create_config(test_file_);
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result->layout.size(), 5);
    EXPECT_EQ(result->layout[0].type, LayoutType::Stocks);
    EXPECT_EQ(result->layout[1].type, LayoutType::Video);
    EXPECT_EQ(result->layout[1].video_index, 2);
    EXPECT_EQ(result->layout[2].type, LayoutType::Weather);
    EXPECT_EQ(result->layout[3].type, LayoutType::News);
    EXPECT_EQ(result->layout[4].type, LayoutType::Video);
    EXPECT_EQ(result->layout[4].video_index, 0);
}

TEST_F(ConfigModuleTest, DefaultLayoutWhenMissing) {
    nlohmann::json j = {
        {"location", {{"name", "London"}, {"lat", 51.5}, {"lon", -0.1}}},
        {"stocks", nlohmann::json::array()},
        {"videos", {
            {{"enabled", true}, {"playlists", {"a.mp4"}}},
            {{"enabled", true}, {"playlists", {"b.mp4"}}}
        }}
    };
    std::ofstream out(test_file_);
    out << j.dump();
    out.close();

    ConfigModule config;
    auto result = config.load_or_create_config(test_file_);
    ASSERT_TRUE(result.has_value());
    // Default: weather, stocks, news, then one entry per video
    ASSERT_EQ(result->layout.size(), 5);
    EXPECT_EQ(result->layout[0].type, LayoutType::Weather);
    EXPECT_EQ(result->layout[1].type, LayoutType::Stocks);
    EXPECT_EQ(result->layout[2].type, LayoutType::News);
    EXPECT_EQ(result->layout[3].type, LayoutType::Video);
    EXPECT_EQ(result->layout[3].video_index, 0);
    EXPECT_EQ(result->layout[4].type, LayoutType::Video);
    EXPECT_EQ(result->layout[4].video_index, 1);
}
#include "modules/news_module.hpp"
#include "stubs_alsa.hpp" // Contains CurlMockState

using namespace nuc_display::tests::mock;

TEST(NewsModuleTest, UserAgentAndFallback) {
    g_curl_mock.reset();
    
    // Mock Google News failure
    g_curl_mock.mock_errors["https://news.google.com/rss/search?q=stock+market&hl=en-US&gl=US&ceid=US:en"] = CURLE_COULDNT_CONNECT;
    
    std::vector<std::string> urls = {
        "https://news.google.com/rss/search?q=stock+market&hl=en-US&gl=US&ceid=US:en",
        "http://feeds.bbci.co.uk/news/rss.xml"
    };
    NewsModule module;
    module.update_headlines(urls);
    
    // Verify it set the User-Agent
    EXPECT_FALSE(g_curl_mock.last_user_agent.empty());
    EXPECT_THAT(g_curl_mock.last_user_agent, ::testing::HasSubstr("Mozilla/5.0"));
    
    // Verify it requested Google News first
    ASSERT_GE(g_curl_mock.requested_urls.size(), 2u);
    EXPECT_EQ(g_curl_mock.requested_urls[0], "https://news.google.com/rss/search?q=stock+market&hl=en-US&gl=US&ceid=US:en");
    
    // Verify it fell back to BBC News
    EXPECT_EQ(g_curl_mock.requested_urls[1], "http://feeds.bbci.co.uk/news/rss.xml");
}

TEST(NewsModuleTest, MalformedAndEmptyRSS) {
    g_curl_mock.reset();
    
    // Google News returns malformed XML
    g_curl_mock.mock_responses["https://news.google.com/rss/search?q=stock+market&hl=en-US&gl=US&ceid=US:en"] = "<rss><item><title>Broken CDATA";
    // BBC returns empty string
    g_curl_mock.mock_responses["http://feeds.bbci.co.uk/news/rss.xml"] = "";
    
    std::vector<std::string> urls = {
        "https://news.google.com/rss/search?q=stock+market&hl=en-US&gl=US&ceid=US:en",
        "http://feeds.bbci.co.uk/news/rss.xml"
    };
    NewsModule module;
    module.update_headlines(urls);
    
    // Verify both were requested
    EXPECT_EQ(g_curl_mock.requested_urls.size(), 2u);
    // Note: Since everything failed, it shouldn't have any items
    // But we are mainly testing for lack of crashes here.
    SUCCEED();
}

#include "modules/http_server_module.hpp"
#include "modules/input_module.hpp"
#include <thread>
#include <chrono>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

// Perform HTTP request using raw socket to bypass global Curl link-time stubs
static std::string socket_http_request(const std::string& method, int port, const std::string& path, const std::string& body = "", const std::vector<std::string>& extra_headers = {}, long* response_code = nullptr) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return "";
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        close(sock);
        return "";
    }
    
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return "";
    }
    
    // Construct HTTP/1.1 request
    std::stringstream req;
    req << method << " " << path << " HTTP/1.1\r\n"
        << "Host: 127.0.0.1:" << port << "\r\n"
        << "Connection: close\r\n";
        
    for (const auto& h : extra_headers) {
        req << h << "\r\n";
    }
    
    if (!body.empty()) {
        req << "Content-Length: " << body.length() << "\r\n";
    }
    req << "\r\n" << body;
    
    std::string request_str = req.str();
    send(sock, request_str.c_str(), request_str.length(), 0);
    
    // Read raw HTTP response
    std::string response;
    char buffer[4096];
    int n;
    while ((n = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[n] = '\0';
        response.append(buffer, n);
    }
    close(sock);
    
    // Parse HTTP response code and split body
    size_t header_end = response.find("\r\n\r\n");
    if (header_end == std::string::npos) return "";
    
    std::string headers = response.substr(0, header_end);
    std::string resp_body = response.substr(header_end + 4);
    
    if (response_code) {
        size_t status_pos = headers.find("HTTP/1.1 ");
        if (status_pos != std::string::npos) {
            *response_code = std::stol(headers.substr(status_pos + 9, 3));
        }
    }
    
    return resp_body;
}

TEST(HttpServerModuleTest, BasicVerification) {
    InputModule input;
    std::atomic<bool> reload_flag{false};
    
    // Start server on a non-conflicting port
    HttpServerModule server(&input, "config.json", reload_flag, 9999);
    
    EXPECT_EQ(server.get_port(), 9999);
    EXPECT_FALSE(server.get_ip_address().empty());
    EXPECT_THAT(server.get_web_address(), ::testing::HasSubstr("9999"));
    
    // Check QR code image generation
    EXPECT_TRUE(server.has_qr_code_updates());
    auto qr = server.get_qr_code_image();
    EXPECT_GT(qr.size, 0);
    EXPECT_EQ(qr.rgba_pixels.size(), static_cast<size_t>(qr.size * qr.size * 4));
    EXPECT_FALSE(server.has_qr_code_updates());
}

TEST(HttpServerModuleTest, MediaManagementAPI) {
    InputModule input;
    std::atomic<bool> reload_flag{false};
    
    // Start server on a non-conflicting port
    HttpServerModule server(&input, "config.json", reload_flag, 9991);
    server.start();
    
    // Give the server a small moment to spin up the listening thread
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Clean up test file if it exists prior
    std::remove("assets/media/test_upload.txt");
    
    // 1. GET /api/media (should contain listing or be empty)
    long code = 0;
    std::string res = socket_http_request("GET", 9991, "/api/media", "", {}, &code);
    EXPECT_EQ(code, 200);
    nlohmann::json files = nlohmann::json::parse(res);
    EXPECT_TRUE(files.is_array());
    
    // 2. POST /api/upload
    std::vector<std::string> headers = {
        "X-Filename: test_upload.txt",
        "Content-Type: application/octet-stream"
    };
    std::string content = "This is a test media asset file content.";
    res = socket_http_request("POST", 9991, "/api/upload", content, headers, &code);
    EXPECT_EQ(code, 200);
    nlohmann::json upload_res = nlohmann::json::parse(res);
    EXPECT_EQ(upload_res["status"], "ok");
    
    // Check that file was written to disk
    std::ifstream in("assets/media/test_upload.txt");
    ASSERT_TRUE(in.is_open());
    std::string file_content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(file_content, content);
    in.close();
    
    // 3. GET /api/media (should now contain test_upload.txt)
    res = socket_http_request("GET", 9991, "/api/media", "", {}, &code);
    EXPECT_EQ(code, 200);
    files = nlohmann::json::parse(res);
    bool found = false;
    for (const auto& f : files) {
        if (f["name"] == "test_upload.txt") {
            found = true;
            EXPECT_EQ(f["size"], content.length());
        }
    }
    EXPECT_TRUE(found);
    
    // 4. GET /api/media/download?file=test_upload.txt
    res = socket_http_request("GET", 9991, "/api/media/download?file=test_upload.txt", "", {}, &code);
    EXPECT_EQ(code, 200);
    EXPECT_EQ(res, content);
    
    // 5. POST /api/media/delete
    nlohmann::json del_body;
    del_body["file"] = "test_upload.txt";
    res = socket_http_request("POST", 9991, "/api/media/delete", del_body.dump(), {"Content-Type: application/json"}, &code);
    EXPECT_EQ(code, 200);
    nlohmann::json del_res = nlohmann::json::parse(res);
    EXPECT_EQ(del_res["status"], "ok");
    
    // Check that file was deleted from disk
    std::ifstream in2("assets/media/test_upload.txt");
    EXPECT_FALSE(in2.is_open());
    
    server.stop();
}

TEST(HttpServerModuleTest, VideoTriggerAPI) {
    InputModule input;
    std::atomic<bool> reload_flag{false};
    
    HttpServerModule server(&input, "config.json", reload_flag, 9992);
    server.start();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    
    // Test 1: Successful video trigger index
    long code = 0;
    nlohmann::json trigger_body;
    trigger_body["video_index"] = 0;
    std::string res = socket_http_request("POST", 9992, "/api/video/trigger", trigger_body.dump(), {"Content-Type: application/json"}, &code);
    EXPECT_EQ(code, 200);
    nlohmann::json trigger_res = nlohmann::json::parse(res);
    EXPECT_EQ(trigger_res["status"], "ok");
    
    // Check pop_video_trigger returns 0
    auto pop_res = server.pop_video_trigger();
    ASSERT_TRUE(pop_res.has_value());
    EXPECT_EQ(pop_res.value(), 0);
    
    // Check subsequent pop returns empty
    EXPECT_FALSE(server.pop_video_trigger().has_value());

    // Test 2: Trigger out-of-bounds video index
    trigger_body["video_index"] = 999;
    res = socket_http_request("POST", 9992, "/api/video/trigger", trigger_body.dump(), {"Content-Type: application/json"}, &code);
    EXPECT_EQ(code, 400);
    nlohmann::json err_res = nlohmann::json::parse(res);
    EXPECT_TRUE(err_res.contains("error"));
    
    server.stop();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}


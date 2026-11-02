#include "windows_standalone_host.hpp"

#if defined(_WIN32)

#include <pulp/view/plugin_view_host.hpp>
#include <pulp/view/window_host.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace spectr {
namespace {

class WindowsStandaloneHost final : public pulp::view::WindowHost {
public:
    WindowsStandaloneHost(pulp::view::View& root,
                          const pulp::view::WindowOptions& options)
        : root_(root), options_(options) {
        register_class();

        const auto title = utf8_to_wide(options_.title);
        hwnd_ = CreateWindowExW(
            0, class_name(), title.c_str(),
            WS_OVERLAPPEDWINDOW | (options_.resizable ? 0 : WS_THICKFRAME),
            CW_USEDEFAULT, CW_USEDEFAULT,
            std::max(320, static_cast<int>(options_.width)),
            std::max(240, static_cast<int>(options_.height)),
            nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd_) return;

        pulp::view::PluginViewHost::Options plugin_options;
        plugin_options.size = {
            static_cast<uint32_t>(std::max(1.0f, options_.width)),
            static_cast<uint32_t>(std::max(1.0f, options_.height))};
        plugin_options.use_gpu = options_.use_gpu;
        plugin_options.present_policy =
            pulp::view::PluginViewHost::PresentPolicy::vsync;
        child_ = pulp::view::PluginViewHost::create(root_, plugin_options);
        if (!child_) return;

        child_->set_resize_callback([this](uint32_t width, uint32_t height) {
            if (resize_callback_) resize_callback_(width, height);
        });
        (void)child_->try_attach_to_parent(reinterpret_cast<void*>(hwnd_));
        resize_child();
        SetTimer(hwnd_, timer_id, 16, nullptr);
    }

    ~WindowsStandaloneHost() override {
        if (hwnd_) KillTimer(hwnd_, timer_id);
        if (child_) child_->detach();
        if (hwnd_) DestroyWindow(hwnd_);
    }

    bool valid() const { return hwnd_ != nullptr && child_ != nullptr && child_->is_attached(); }

    void show() override {
        if (hwnd_) {
            ShowWindow(hwnd_, SW_SHOWNORMAL);
            UpdateWindow(hwnd_);
        }
    }

    void hide() override {
        if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
    }

    bool is_visible() const override {
        return hwnd_ && IsWindowVisible(hwnd_) != FALSE;
    }

    void repaint() override {
        if (child_) child_->repaint();
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void set_close_callback(std::function<void()> callback) override {
        close_callback_ = std::move(callback);
    }

    void run_event_loop() override {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (close_requested_) break;
        }
    }

    void request_close() override {
        if (hwnd_) PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }

    void set_idle_callback(std::function<void()> callback) override {
        idle_callback_ = std::move(callback);
    }

    void set_resize_callback(ResizeCallback callback) override {
        resize_callback_ = std::move(callback);
    }

    void request_content_size(float width, float height) override {
        if (!hwnd_) return;
        RECT frame{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
        AdjustWindowRectEx(&frame, WS_OVERLAPPEDWINDOW, FALSE, 0);
        SetWindowPos(hwnd_, nullptr, 0, 0,
                     frame.right - frame.left, frame.bottom - frame.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void set_fixed_aspect_ratio(float ratio) override { aspect_ratio_ = ratio; }

    void* native_window_handle() const override {
        return reinterpret_cast<void*>(hwnd_);
    }

    void* native_content_view_handle() const override {
        return child_ ? child_->native_handle() : nullptr;
    }

    bool is_gpu_backed() const override {
        return child_ && child_->is_gpu_backed();
    }

    pulp::view::WindowHost::ContentSize get_content_size() const override {
        RECT rect{};
        if (!hwnd_ || !GetClientRect(hwnd_, &rect)) return {};
        return {static_cast<uint32_t>(std::max<LONG>(0, rect.right - rect.left)),
                static_cast<uint32_t>(std::max<LONG>(0, rect.bottom - rect.top))};
    }

private:
    static constexpr UINT_PTR timer_id = 1;

    static const wchar_t* class_name() { return L"SpectrWindowsStandalone"; }

    static std::wstring utf8_to_wide(const std::string& text) {
        if (text.empty()) return L"Spectr";
        const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(),
                                              static_cast<int>(text.size()),
                                              nullptr, 0);
        std::wstring result(static_cast<size_t>(count), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(),
                            static_cast<int>(text.size()), result.data(), count);
        return result;
    }

    static void register_class() {
        static std::once_flag once;
        std::call_once(once, [] {
            WNDCLASSEXW klass{};
            klass.cbSize = sizeof(klass);
            klass.lpfnWndProc = &WindowsStandaloneHost::window_proc;
            klass.hInstance = GetModuleHandleW(nullptr);
            klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            klass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
            klass.lpszClassName = class_name();
            RegisterClassExW(&klass);
        });
    }

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message,
                                        WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<WindowsStandaloneHost*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            self = static_cast<WindowsStandaloneHost*>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(hwnd, message, wparam, lparam);

        switch (message) {
        case WM_SIZE:
            self->resize_child();
            return 0;
        case WM_TIMER:
            if (wparam == timer_id && self->idle_callback_) {
                self->idle_callback_();
                self->repaint();
            }
            return 0;
        case WM_CLOSE:
            self->close_requested_ = true;
            if (self->close_callback_) self->close_callback_();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }

    void resize_child() {
        if (!hwnd_ || !child_) return;
        RECT rect{};
        if (!GetClientRect(hwnd_, &rect)) return;
        const auto width = static_cast<uint32_t>(std::max<LONG>(1, rect.right - rect.left));
        const auto height = static_cast<uint32_t>(std::max<LONG>(1, rect.bottom - rect.top));
        child_->set_size(width, height);
        if (resize_callback_) resize_callback_(width, height);
    }

    pulp::view::View& root_;
    pulp::view::WindowOptions options_;
    HWND hwnd_ = nullptr;
    std::unique_ptr<pulp::view::PluginViewHost> child_;
    std::function<void()> close_callback_;
    std::function<void()> idle_callback_;
    ResizeCallback resize_callback_;
    bool close_requested_ = false;
    float aspect_ratio_ = 0.0f;
};

} // namespace

void install_windows_standalone_window_host() {
    pulp::view::WindowHost::set_factory(
        [](pulp::view::View& root, const pulp::view::WindowOptions& options)
            -> std::unique_ptr<pulp::view::WindowHost> {
            auto host = std::make_unique<WindowsStandaloneHost>(root, options);
            if (!host->valid()) return nullptr;
            return host;
        });
}

} // namespace spectr

#endif

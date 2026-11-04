#include "windows_standalone_host.hpp"
#include "spectr/test_seams.hpp"

#if defined(_WIN32)

#include <pulp/view/plugin_view_host.hpp>
#include <pulp/view/window_host.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace spectr {
namespace {

UINT window_dpi(HWND hwnd) {
    const auto dpi = GetDpiForWindow(hwnd);
    return dpi != 0 ? dpi : 96;
}

class WindowsStandaloneHost final : public pulp::view::WindowHost {
public:
    WindowsStandaloneHost(pulp::view::View& root,
                          const pulp::view::WindowOptions& options)
        : root_(root), options_(options) {
        register_class();

        const auto title = utf8_to_wide(options_.title);
        const DWORD style = options_.resizable
            ? WS_OVERLAPPEDWINDOW
            : WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        // WindowOptions describes the content. CreateWindowEx instead takes
        // the outer frame, including the caption and borders. Passing the
        // content size directly clips the editor's bottom and right edges.
        const auto dpi = GetDpiForSystem();
        RECT frame{0, 0,
                   MulDiv(std::max(320, static_cast<int>(options_.width)), dpi, 96),
                   MulDiv(std::max(240, static_cast<int>(options_.height)), dpi, 96)};
        if (!AdjustWindowRectExForDpi(&frame, style, FALSE, 0, dpi))
            return;
        hwnd_ = CreateWindowExW(
            0, class_name(), title.c_str(),
            style,
            CW_USEDEFAULT, CW_USEDEFAULT,
            frame.right - frame.left, frame.bottom - frame.top,
            nullptr, nullptr, GetModuleHandleW(nullptr), this);
        if (!hwnd_) return;

        pulp::view::PluginViewHost::Options plugin_options;
        plugin_options.size = {
            static_cast<uint32_t>(std::max(1.0f, options_.width)),
            static_cast<uint32_t>(std::max(1.0f, options_.height))};
        plugin_options.use_gpu = options_.use_gpu;
        plugin_options.background_rgb = options_.background_rgb;
        plugin_options.present_policy =
            pulp::view::PluginViewHost::PresentPolicy::vsync;
#if defined(SPECTR_ENABLE_TEST_SEAMS)
        // Explicit development experiments, absent from shipping binaries.
        // CPU raster and timed WARP distinguish scene cost from presentation
        // waits without changing the renderer selected for ordinary launches.
        if (const auto* probe = SPECTR_TEST_ENV("SPECTR_WINDOWS_RENDER_PROBE")) {
            const std::string_view mode{probe};
            if (mode == "cpu") plugin_options.use_gpu = false;
            if (mode == "gpu-timed") plugin_options.enable_gpu_timing = true;
            std::fprintf(stderr, "[spectr-render-probe] mode=%s\n", probe);
        }
#endif
        child_ = pulp::view::PluginViewHost::create(root_, plugin_options);
        if (!child_) return;

        child_->set_resize_callback([this](uint32_t width, uint32_t height) {
            if (resize_callback_) resize_callback_(width, height);
        });
        if (!child_->try_attach_to_parent(reinterpret_cast<void*>(hwnd_))) {
            std::fprintf(stderr, "Spectr: standalone editor attachment failed\n");
            return;
        }
        resize_child();
        // Pulp's standalone lifecycle can create and attach the host without
        // issuing WindowHost::show() on this Windows path. The owned top-level
        // window must be made visible here or the process remains headless
        // while audio and GPU rendering continue normally.
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        UpdateWindow(hwnd_);
    }

    ~WindowsStandaloneHost() override {
        if (child_) child_->detach();
        if (hwnd_) DestroyWindow(hwnd_);
    }

    // A created child is not necessarily attached: native parenting or its
    // frame timer can fail. Require the SDK's actual attachment state before
    // accepting this host, so a failed child cannot leave an empty window.
    bool valid() const {
        return hwnd_ != nullptr && child_ != nullptr && child_->is_attached();
    }

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
        // The child owns all client pixels and its frame timer coalesces
        // repaints. Invalidating the parent here creates higher-priority
        // WM_PAINT work on every scripted repaint and starves that timer.
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
        // Match the DAW adapters: poll editor work from the child renderer's
        // idle callback. A second parent timer can starve behind a slow child
        // frame and leave analyzer/session work detached from rendered frames.
        if (child_) child_->set_idle_callback(std::move(callback));
    }

    void set_resize_callback(ResizeCallback callback) override {
        resize_callback_ = std::move(callback);
    }

    void request_content_size(float width, float height) override {
        if (!hwnd_) return;
        const auto dpi = window_dpi(hwnd_);
        RECT frame{0, 0, MulDiv(static_cast<int>(width), dpi, 96),
                        MulDiv(static_cast<int>(height), dpi, 96)};
        const auto style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_STYLE));
        const auto ex_style = static_cast<DWORD>(GetWindowLongPtrW(hwnd_, GWL_EXSTYLE));
        if (!AdjustWindowRectExForDpi(&frame, style, FALSE, ex_style,
                                     dpi))
            return;
        SetWindowPos(hwnd_, nullptr, 0, 0,
                     frame.right - frame.left, frame.bottom - frame.top,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void set_design_viewport(float width, float height) override {
        // The standalone lifecycle pins the editor's authored coordinate
        // space here. The child owns both paint scaling and inverse mouse
        // mapping; dropping this request clips the document on smaller windows.
        design_width_ = width;
        design_height_ = height;
        if (child_) child_->set_design_viewport(width, height);
    }

    bool design_viewport_transform(float& sx, float& sy,
                                   float& tx, float& ty) const override {
        if (!child_) return false;
        const auto size = child_->get_size();
        return compute_design_viewport_transform(
            static_cast<float>(size.width), static_cast<float>(size.height),
            design_width_, design_height_, sx, sy, tx, ty);
    }

    void set_fixed_aspect_ratio(float ratio) override {
        aspect_ratio_ = std::isfinite(ratio) && ratio > 0.0f ? ratio : 0.0f;
        if (child_) child_->set_fixed_aspect_ratio(aspect_ratio_);
    }

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
        if (!child_) return {};
        const auto size = child_->get_size();
        return {size.width, size.height};
    }

private:

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
        case WM_GETMINMAXINFO: {
            // Keep Windows' monitor/work-area calculation, then fit the
            // authored client aspect inside it, including the native frame.
            const auto result = DefWindowProcW(hwnd, message, wparam, lparam);
            self->constrain_window_limits(hwnd, *reinterpret_cast<MINMAXINFO*>(lparam));
            return result;
        }
        case WM_SIZING:
            if (self->constrain_resize(hwnd, static_cast<UINT>(wparam),
                                       *reinterpret_cast<RECT*>(lparam)))
                return TRUE;
            break;
        case WM_SIZE:
            self->resize_child();
            return 0;
        case WM_DPICHANGED: {
            // The suggested outer frame keeps the logical content size when
            // moving between monitors. The child API also uses logical units.
            if (self->child_) {
                const auto message_dpi = HIWORD(wparam);
                const auto dpi = message_dpi != 0 ? message_dpi : window_dpi(hwnd);
                self->child_->set_scale_factor(static_cast<float>(dpi) / 96.0f);
            }
            const auto* frame = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, frame->left, frame->top,
                         frame->right - frame->left, frame->bottom - frame->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            self->resize_child();
            return 0;
        }
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
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    bool window_frame(HWND hwnd, LONG& width, LONG& height) const {
        RECT frame{};
        const auto style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_STYLE));
        const auto ex_style = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
        if (!AdjustWindowRectExForDpi(&frame, style, FALSE, ex_style, window_dpi(hwnd)))
            return false;
        width = frame.right - frame.left;
        height = frame.bottom - frame.top;
        return true;
    }

    void constrain_window_limits(HWND hwnd, MINMAXINFO& limits) const {
        LONG frame_w = 0, frame_h = 0;
        if (!window_frame(hwnd, frame_w, frame_h)) return;
        const double scale = static_cast<double>(window_dpi(hwnd)) / 96.0;
        double min_w = std::max(0.0, static_cast<double>(options_.min_width) * scale);
        double min_h = std::max(0.0, static_cast<double>(options_.min_height) * scale);
        if (aspect_ratio_ > 0.0f) {
            min_w = std::max(min_w, min_h * aspect_ratio_);
            min_h = min_w / aspect_ratio_;

            const auto available_w = std::max(1L, limits.ptMaxSize.x - frame_w);
            const auto available_h = std::max(1L, limits.ptMaxSize.y - frame_h);
            const double width = std::min(static_cast<double>(available_w),
                                           available_h * static_cast<double>(aspect_ratio_));
            const LONG outer_w = static_cast<LONG>(std::lround(width)) + frame_w;
            const LONG outer_h = static_cast<LONG>(std::lround(width / aspect_ratio_)) + frame_h;
            limits.ptMaxPosition.x += (limits.ptMaxSize.x - outer_w) / 2;
            limits.ptMaxPosition.y += (limits.ptMaxSize.y - outer_h) / 2;
            limits.ptMaxSize = {outer_w, outer_h};
        }
        limits.ptMinTrackSize.x = std::max(limits.ptMinTrackSize.x,
            static_cast<LONG>(std::ceil(min_w)) + frame_w);
        limits.ptMinTrackSize.y = std::max(limits.ptMinTrackSize.y,
            static_cast<LONG>(std::ceil(min_h)) + frame_h);
    }

    bool constrain_resize(HWND hwnd, UINT edge, RECT& proposed) const {
        if (aspect_ratio_ <= 0.0f) return false;
        LONG frame_w = 0, frame_h = 0;
        RECT current{};
        if (!window_frame(hwnd, frame_w, frame_h) || !GetWindowRect(hwnd, &current))
            return false;
        const auto width = std::max(1L, proposed.right - proposed.left - frame_w);
        const auto height = std::max(1L, proposed.bottom - proposed.top - frame_h);
        const auto dx = (proposed.right - proposed.left) - (current.right - current.left);
        const auto dy = (proposed.bottom - proposed.top) - (current.bottom - current.top);
        const bool height_driven = edge == WMSZ_TOP || edge == WMSZ_BOTTOM ||
            ((edge != WMSZ_LEFT && edge != WMSZ_RIGHT) &&
             std::abs(static_cast<double>(dy) * aspect_ratio_) > std::abs(static_cast<double>(dx)));
        const double scale = static_cast<double>(window_dpi(hwnd)) / 96.0;
        const double client_w = std::max({
            height_driven ? height * static_cast<double>(aspect_ratio_) : static_cast<double>(width),
            static_cast<double>(options_.min_width) * scale,
            static_cast<double>(options_.min_height) * scale * aspect_ratio_});
        const LONG outer_w = static_cast<LONG>(std::lround(client_w)) + frame_w;
        const LONG outer_h = static_cast<LONG>(std::lround(client_w / aspect_ratio_)) + frame_h;
        if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT)
            proposed.left = proposed.right - outer_w;
        else
            proposed.right = proposed.left + outer_w;
        if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT)
            proposed.top = proposed.bottom - outer_h;
        else
            proposed.bottom = proposed.top + outer_h;
        return true;
    }

    void resize_child() {
        if (!hwnd_ || !child_) return;
        RECT rect{};
        if (!GetClientRect(hwnd_, &rect)) return;
        const auto dpi = window_dpi(hwnd_);
        child_->set_scale_factor(static_cast<float>(dpi) / 96.0f);
        const auto width = static_cast<uint32_t>(std::max(1, MulDiv(rect.right - rect.left, 96, dpi)));
        const auto height = static_cast<uint32_t>(std::max(1, MulDiv(rect.bottom - rect.top, 96, dpi)));
        child_->set_size(width, height);
        if (resize_callback_) resize_callback_(width, height);
    }

    pulp::view::View& root_;
    pulp::view::WindowOptions options_;
    HWND hwnd_ = nullptr;
    std::unique_ptr<pulp::view::PluginViewHost> child_;
    std::function<void()> close_callback_;
    ResizeCallback resize_callback_;
    bool close_requested_ = false;
    float aspect_ratio_ = 0.0f;
    float design_width_ = 0.0f;
    float design_height_ = 0.0f;
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

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <chrono>
#include <memory>
#include <sstream>
#include <string>

#include "DiagnosticsLog.h"
#include "DX11Renderer.h"
#include "DX12Renderer.h"
#include "IRendererBackend.h"

namespace dxteaching
{

namespace
{

constexpr const char *kWindowClassName = "GraphicsAPITeachingDemoDXWindow";

constexpr UINT kMenuTopicBase = 1000;
constexpr UINT kMenuTopicMax = kMenuTopicBase + kTopicCount - 1;
constexpr UINT kMenuToggleError = 2001;
constexpr UINT kMenuBackendDX11 = 2101;
constexpr UINT kMenuBackendDX12 = 2102;
constexpr UINT kMenuExit = 2199;
constexpr UINT_PTR kRenderTimerId = 1;
constexpr UINT kRenderTimerIntervalMs = 16;

class DemoApplication
{
public:
    bool Initialize(HINSTANCE instance, int showCommand)
    {
        ResetLogFile();
        LogLine("APP", "Initialize begin");
        instance_ = instance;

        WNDCLASSEXA windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = &DemoApplication::WndProc;
        windowClass.hInstance = instance_;
        windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kWindowClassName;

        if (!RegisterClassExA(&windowClass))
        {
            LogLine("APP", "RegisterClassExA failed");
            return false;
        }

        RECT rect{0, 0, static_cast<LONG>(width_), static_cast<LONG>(height_)};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, TRUE);

        hwnd_ = CreateWindowExA(0,
                                kWindowClassName,
                                "Graphics API Teaching Demo",
                                WS_OVERLAPPEDWINDOW,
                                CW_USEDEFAULT,
                                CW_USEDEFAULT,
                                rect.right - rect.left,
                                rect.bottom - rect.top,
                                nullptr,
                                nullptr,
                                instance_,
                                this);
        if (!hwnd_)
        {
            LogLine("APP", "CreateWindowExA failed");
            return false;
        }

        CreateMenus();

        ShowWindow(hwnd_, showCommand);
        UpdateWindow(hwnd_);

        if (!CreateRenderer(BackendType::DX12) && !CreateRenderer(BackendType::DX11))
        {
            LogLine("APP", "CreateRenderer failed for both backends");
            MessageBoxA(hwnd_, "Failed to initialize both DX12 and DX11 renderers.", "Initialization Error", MB_ICONERROR);
            return false;
        }

        topic_ = 1;
        errorExampleEnabled_ = false;
        startTime_ = std::chrono::steady_clock::now();

        if (SetTimer(hwnd_, kRenderTimerId, kRenderTimerIntervalMs, nullptr) == 0)
        {
            LogLine("APP", "SetTimer failed");
            return false;
        }

        LogLine("APP", "Initialize done, backend=%s, size=%ux%u", renderer_ ? renderer_->BackendName() : "None", width_, height_);

        UpdateMenuChecks();
        UpdateWindowTitle();
        return true;
    }

    int Run()
    {
        LogLine("APP", "Run loop entered");
        MSG msg{};
        while (GetMessage(&msg, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        LogLine("APP", "Run loop exited, wParam=%lld", static_cast<long long>(msg.wParam));
        return static_cast<int>(msg.wParam);
    }

    void RenderFrame()
    {
        if (renderingInProgress_ || minimized_ || !renderer_ || width_ == 0 || height_ == 0)
        {
            return;
        }

        renderingInProgress_ = true;
        struct RenderScopeGuard
        {
            bool &flag;
            ~RenderScopeGuard()
            {
                flag = false;
            }
        } guard{renderingInProgress_};

        const auto now = std::chrono::steady_clock::now();
        const float elapsedSeconds = std::chrono::duration<float>(now - startTime_).count();

        FrameSettings settings{};
        settings.elapsedSeconds = elapsedSeconds;
        settings.topic = topic_;
        settings.errorExampleEnabled = errorExampleEnabled_;
        settings.width = width_;
        settings.height = height_;

        renderer_->Render(settings);

        ++frameCounter_;
        if ((frameCounter_ % 120u) == 0u)
        {
            LogLine("APP", "Render heartbeat frame=%llu backend=%s topic=%d size=%ux%u", static_cast<unsigned long long>(frameCounter_), renderer_->BackendName(), topic_, width_, height_);
        }
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
            case WM_SIZE:
            {
                width_ = LOWORD(lParam);
                height_ = HIWORD(lParam);
                minimized_ = (wParam == SIZE_MINIMIZED);
                LogLine("APP", "WM_SIZE w=%u h=%u minimized=%d", width_, height_, minimized_ ? 1 : 0);

                if (!minimized_ && renderer_ && width_ > 0 && height_ > 0)
                {
                    renderer_->Resize(width_, height_);
                }
                return 0;
            }

            case WM_COMMAND:
            {
                const UINT commandId = LOWORD(wParam);
                LogLine("APP", "WM_COMMAND id=%u", commandId);
                if (commandId >= kMenuTopicBase && commandId <= kMenuTopicMax)
                {
                    SetTopic(static_cast<int>(commandId - kMenuTopicBase + 1));
                    return 0;
                }

                switch (commandId)
                {
                    case kMenuToggleError:
                        ToggleErrorExample();
                        return 0;
                    case kMenuBackendDX11:
                        SwitchBackend(BackendType::DX11);
                        return 0;
                    case kMenuBackendDX12:
                        SwitchBackend(BackendType::DX12);
                        return 0;
                    case kMenuExit:
                        DestroyWindow(hwnd_);
                        return 0;
                    default:
                        break;
                }
                break;
            }

            case WM_KEYDOWN:
                LogLine("APP", "WM_KEYDOWN key=%u", static_cast<unsigned>(wParam));
            {
                if (wParam >= '1' && wParam <= '9')
                {
                    SetTopic(static_cast<int>(wParam - '0'));
                    return 0;
                }

                if (wParam >= VK_NUMPAD1 && wParam <= VK_NUMPAD9)
                {
                    SetTopic(static_cast<int>(wParam - VK_NUMPAD0));
                    return 0;
                }

                switch (wParam)
                {
                    case VK_F1:
                        SetTopic(10);
                        return 0;
                    case VK_F2:
                        SetTopic(11);
                        return 0;
                    case VK_F3:
                        SetTopic(12);
                        return 0;
                    case VK_F4:
                        SetTopic(13);
                        return 0;
                    case VK_F5:
                        SetTopic(14);
                        return 0;
                    case VK_F6:
                        SetTopic(15);
                        return 0;
                    case VK_F7:
                        SetTopic(16);
                        return 0;
                    case VK_F8:
                        SetTopic(17);
                        return 0;
                    case VK_F9:
                        SetTopic(18);
                        return 0;
                    case VK_F10:
                        SetTopic(19);
                        return 0;
                    case VK_F11:
                        SetTopic(20);
                        return 0;
                    case 'Q':
                        SwitchBackend(BackendType::DX11);
                        return 0;
                    case 'W':
                        SwitchBackend(BackendType::DX12);
                        return 0;
                    case 'E':
                        ToggleErrorExample();
                        return 0;
                    default:
                        break;
                }
                break;
            }

            case WM_TIMER:
                if (wParam == kRenderTimerId)
                {
                    RenderFrame();
                    return 0;
                }
                break;

            case WM_ENTERIDLE:
                // Keep the scene alive while menu modal loops are active.
                if (wParam == MSGF_MENU)
                {
                    RenderFrame();
                    return 0;
                }
                break;

            case WM_DESTROY:
                LogLine("APP", "WM_DESTROY");
                KillTimer(hwnd_, kRenderTimerId);
                if (renderer_)
                {
                    renderer_->Shutdown();
                    renderer_.reset();
                }
                PostQuitMessage(0);
                return 0;

            default:
                break;
        }

        return DefWindowProcA(hwnd_, message, wParam, lParam);
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        DemoApplication *app = reinterpret_cast<DemoApplication *>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            const auto *createStruct = reinterpret_cast<const CREATESTRUCTA *>(lParam);
            app = static_cast<DemoApplication *>(createStruct->lpCreateParams);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
            app->hwnd_ = hwnd;
        }

        if (app)
        {
            return app->HandleMessage(message, wParam, lParam);
        }

        return DefWindowProcA(hwnd, message, wParam, lParam);
    }

private:
    void CreateMenus()
    {
        HMENU mainMenu = CreateMenu();
        HMENU demoMenu = CreatePopupMenu();
        HMENU rendererMenu = CreatePopupMenu();

        for (int topic = 1; topic <= static_cast<int>(kTopicCount); ++topic)
        {
            std::ostringstream label;
            label << topic << ". " << TopicTitle(topic);
            AppendMenuA(demoMenu,
                        MF_STRING,
                        static_cast<UINT_PTR>(kMenuTopicBase + (topic - 1)),
                        label.str().c_str());
        }

        AppendMenuA(demoMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(demoMenu, MF_STRING, kMenuToggleError, "Toggle Error Example\tE");

        AppendMenuA(rendererMenu, MF_STRING, kMenuBackendDX11, "DX11 Renderer\tQ");
        AppendMenuA(rendererMenu, MF_STRING, kMenuBackendDX12, "DX12 Renderer\tW");
        AppendMenuA(rendererMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuA(rendererMenu, MF_STRING, kMenuExit, "Exit");

        AppendMenuA(mainMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(demoMenu), "Demo");
        AppendMenuA(mainMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(rendererMenu), "Renderer");

        SetMenu(hwnd_, mainMenu);
        DrawMenuBar(hwnd_);
    }

    bool CreateRenderer(BackendType backend)
    {
        LogLine("APP", "CreateRenderer begin target=%s", backend == BackendType::DX11 ? "DX11" : "DX12");
        std::unique_ptr<IRendererBackend> candidate;
        if (backend == BackendType::DX11)
        {
            candidate = std::make_unique<DX11Renderer>();
        }
        else
        {
            candidate = std::make_unique<DX12Renderer>();
        }

        if (!candidate->Initialize(hwnd_, width_, height_))
        {
            LogLine("APP", "CreateRenderer failed target=%s", backend == BackendType::DX11 ? "DX11" : "DX12");
            return false;
        }

        renderer_ = std::move(candidate);
        backend_ = backend;
        UpdateMenuChecks();
        UpdateWindowTitle();
        LogLine("APP", "CreateRenderer success active=%s", renderer_->BackendName());
        return true;
    }

    bool SwitchBackend(BackendType backend)
    {
        LogLine("APP", "SwitchBackend request target=%s current=%s", backend == BackendType::DX11 ? "DX11" : "DX12", renderer_ ? renderer_->BackendName() : "None");
        if (backend_ == backend && renderer_)
        {
            LogLine("APP", "SwitchBackend ignored because already active");
            return true;
        }

        const bool timerWasActive = (KillTimer(hwnd_, kRenderTimerId) != 0);
        const BackendType previous = backend_;
        std::unique_ptr<IRendererBackend> previousRenderer = std::move(renderer_);

        if (previousRenderer)
        {
            previousRenderer->Shutdown();
            previousRenderer.reset();
        }

        if (CreateRenderer(backend))
        {
            LogLine("APP", "SwitchBackend success active=%s", renderer_ ? renderer_->BackendName() : "None");
            RestartRenderTimer(timerWasActive);
            RenderFrame();
            return true;
        }

        std::ostringstream error;
        error << "Failed to initialize " << (backend == BackendType::DX11 ? "DX11" : "DX12")
              << " backend. Reverting to previous backend.";
        MessageBoxA(hwnd_, error.str().c_str(), "Renderer Switch Error", MB_ICONWARNING);

        const bool reverted = CreateRenderer(previous);
        LogLine("APP", "SwitchBackend revert result=%d active=%s", reverted ? 1 : 0, renderer_ ? renderer_->BackendName() : "None");
        RestartRenderTimer(timerWasActive);
        if (reverted)
        {
            RenderFrame();
        }
        return reverted;
    }

    void RestartRenderTimer(bool shouldRestart) const
    {
        if (!shouldRestart)
        {
            return;
        }

        if (SetTimer(hwnd_, kRenderTimerId, kRenderTimerIntervalMs, nullptr) == 0)
        {
            LogLine("APP", "SetTimer failed while restarting after backend switch");
        }
    }

    void SetTopic(int topic)
    {
        topic_ = ClampTopic(topic);
        LogLine("APP", "SetTopic topic=%d", topic_);
        UpdateMenuChecks();
        UpdateWindowTitle();
    }

    void ToggleErrorExample()
    {
        errorExampleEnabled_ = !errorExampleEnabled_;
        LogLine("APP", "ToggleErrorExample enabled=%d", errorExampleEnabled_ ? 1 : 0);
        UpdateMenuChecks();
        UpdateWindowTitle();
    }

    void UpdateMenuChecks() const
    {
        HMENU menu = GetMenu(hwnd_);
        if (!menu)
        {
            return;
        }

        CheckMenuRadioItem(menu,
                           kMenuTopicBase,
                           kMenuTopicMax,
                           kMenuTopicBase + static_cast<UINT>(ClampTopic(topic_) - 1),
                           MF_BYCOMMAND);

        const UINT activeBackend = (backend_ == BackendType::DX11) ? kMenuBackendDX11 : kMenuBackendDX12;
        CheckMenuRadioItem(menu,
                           kMenuBackendDX11,
                           kMenuBackendDX12,
                           activeBackend,
                           MF_BYCOMMAND);

        CheckMenuItem(menu,
                      kMenuToggleError,
                      MF_BYCOMMAND | (errorExampleEnabled_ ? MF_CHECKED : MF_UNCHECKED));
    }

    void UpdateWindowTitle() const
    {
        std::ostringstream title;
        title << "Graphics API Teaching Demo [";
        if (renderer_)
        {
            title << renderer_->BackendName();
        }
        else
        {
            title << "No Renderer";
        }
        title << "] Topic " << ClampTopic(topic_) << ": " << TopicTitle(topic_);

        if (errorExampleEnabled_)
        {
            title << " | Error Example ON";
        }

        title << " | Keys: 1-9, F1-F11(10-20), Q=DX11, W=DX12, E=Error";
        SetWindowTextA(hwnd_, title.str().c_str());
    }

private:
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;

    uint32_t width_ = 1280;
    uint32_t height_ = 720;
    bool minimized_ = false;

    BackendType backend_ = BackendType::DX12;
    int topic_ = 1;
    bool errorExampleEnabled_ = false;
    bool renderingInProgress_ = false;
    uint64_t frameCounter_ = 0;

    std::unique_ptr<IRendererBackend> renderer_;
    std::chrono::steady_clock::time_point startTime_{};
};

} // namespace

} // namespace dxteaching

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand)
{
    dxteaching::DemoApplication app;
    if (!app.Initialize(instance, showCommand))
    {
        return 1;
    }

    return app.Run();
}

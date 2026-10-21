// Win32App의 프로젝트·씬 파일 메뉴. 파일 대화상자와 확인 창만 여기서 다루고, 실제 읽기·쓰기와
// 장면 교체는 sr::project::ProjectSession이 BackgroundLoader로 처리한다(Linux 테스트와 같은 경로).
#include "App/Win32App.h"
#include "resource.h"

#include <commdlg.h>

#include <array>
#include <cwctype>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace fs = std::filesystem;

namespace
{
struct ProjectMenuItem
{
    int commandId;
    const wchar_t *label; // 0이면 구분선
};
// 파일 메뉴의 "모델 열기" 아래에 차례로 넣는다.
constexpr std::array projectMenuItems{
    ProjectMenuItem{ID_FILE_NEWPROJECT, L"새 프로젝트(&N)..."},
    ProjectMenuItem{ID_FILE_OPENPROJECT, L"프로젝트 열기(&P)...\tCtrl+Shift+O"},
    ProjectMenuItem{0, nullptr},
    ProjectMenuItem{ID_FILE_NEWSCENE, L"새 씬(&E)..."},
    ProjectMenuItem{ID_FILE_OPENSCENE, L"씬 열기(&C)..."},
    ProjectMenuItem{ID_FILE_SAVESCENE, L"씬 저장(&S)\tCtrl+S"},
    ProjectMenuItem{ID_FILE_SAVESCENEAS, L"다른 이름으로 씬 저장(&A)...\tCtrl+Shift+S"},
    ProjectMenuItem{0, nullptr}};

constexpr wchar_t projectFilter[] = L"Renderer project (*.srproj)\0*.srproj\0";
constexpr wchar_t sceneFilter[] = L"Renderer scene (*.srscene)\0*.srscene\0";

[[nodiscard]] std::wstring utf8ToWideTitle(std::string_view text)
{
    if (text.empty())
        return {};
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring converted(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), converted.data(), length);
    return converted;
}

[[nodiscard]] std::wstring lowerExtension(const fs::path &path)
{
    std::wstring extension = path.extension().wstring();
    for (auto &c : extension)
        c = static_cast<wchar_t>(std::towlower(c));
    return extension;
}

// GetOpenFileNameW/GetSaveFileNameW 공통. 취소하면 nullopt.
[[nodiscard]] std::optional<fs::path> showFileDialog(HWND owner, bool save, const wchar_t *filter,
                                                     const wchar_t *defaultExtension, const wchar_t *title,
                                                     const fs::path &initialDirectory)
{
    std::array<wchar_t, 4096> fileName{};
    const std::wstring directory = initialDirectory.wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = owner;
    dialog.lpstrFilter = filter;
    dialog.lpstrFile = fileName.data();
    dialog.nMaxFile = static_cast<DWORD>(fileName.size());
    dialog.lpstrDefExt = defaultExtension;
    dialog.lpstrTitle = title;
    dialog.lpstrInitialDir = directory.empty() ? nullptr : directory.c_str();
    dialog.Flags =
        OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    const BOOL chosen = save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
    if (!chosen)
        return std::nullopt;
    return fs::path(fileName.data());
}
} // namespace

void Win32App::initializeProjectMenu(HMENU fileMenu, UINT position)
{
    for (const auto &item : projectMenuItems)
    {
        const bool inserted = item.label
                                  ? InsertMenuW(fileMenu, position, MF_BYPOSITION | MF_STRING,
                                                static_cast<UINT_PTR>(item.commandId), item.label)
                                  : InsertMenuW(fileMenu, position, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
        if (!inserted)
            throw std::runtime_error("Failed to initialize project menu");
        ++position;
    }
}

void Win32App::refreshWindowTitle()
{
    m_windowTitle = m_szTitle.data();
    if (const std::string title = m_projectSession.GetTitle(); !title.empty())
        m_windowTitle += L" - " + utf8ToWideTitle(title);
    if (!m_hWnd)
        return;
    const std::wstring text =
        m_windowTitle + L" - AvgFPS: " + std::to_wstring(m_perfAnalyzer.GetAvgFPSForSecond());
    SetWindowTextW(m_hWnd, text.c_str());
}

bool Win32App::confirmDiscardChanges()
{
    if (!m_projectSession.IsDirty())
        return true;
    const int answer = MessageBoxW(m_hWnd,
                                   L"저장하지 않은 변경이 있습니다. "
                                   L"씬을 저장할까요?",
                                   L"Software Renderer", MB_YESNOCANCEL | MB_ICONWARNING);
    m_keys.fill(false); // 모달 동안의 WM_KEYUP은 오지 않는다
    if (answer == IDCANCEL)
        return false;
    if (answer == IDNO)
        return true;
    if (auto saved = m_projectSession.SaveSceneNow(); !saved)
    {
        showAssetError(saved.error());
        return false;
    }
    return true;
}

bool Win32App::openProjectFile(const fs::path &path)
{
    const std::wstring extension = lowerExtension(path);
    if (extension == L".srproj")
    {
        if (confirmDiscardChanges())
            m_projectSession.OpenProject(path);
        return true;
    }
    if (extension == L".srscene")
    {
        if (!m_projectSession.HasProject())
            m_pendingAssetErrors.push_back({"Open the project first, then open its scene", path});
        else if (confirmDiscardChanges())
            m_projectSession.OpenScene(path);
        return true;
    }
    return false;
}

bool Win32App::handleProjectCommand(int commandId)
{
    const bool hasProject = m_projectSession.HasProject();
    const fs::path scenesDirectory = hasProject ? m_projectSession.GetProjectRoot() / L"Scenes" : fs::path{};
    const auto needsProject = [&]
    {
        if (!hasProject)
            m_pendingAssetErrors.push_back(
                {"No project is open. Use File > New project or Open project first."});
        return hasProject;
    };

    std::optional<fs::path> chosen;
    switch (commandId)
    {
    case ID_FILE_NEWPROJECT:
        if (!confirmDiscardChanges())
            break;
        // 고른 폴더 아래에 입력한 이름의 프로젝트 폴더를 만든다: <폴더>\<이름>\<이름>.srproj
        chosen = showFileDialog(m_hWnd, true, projectFilter, L"srproj", L"새 프로젝트: 만들 위치와 이름", {});
        if (chosen)
            m_projectSession.CreateProject(chosen->parent_path() / chosen->stem() / chosen->filename());
        break;
    case ID_FILE_OPENPROJECT:
        if (!confirmDiscardChanges())
            break;
        chosen = showFileDialog(m_hWnd, false, projectFilter, L"srproj", nullptr, {});
        if (chosen)
            m_projectSession.OpenProject(*chosen);
        break;
    case ID_FILE_NEWSCENE:
        if (!needsProject() || !confirmDiscardChanges())
            break;
        chosen = showFileDialog(m_hWnd, true, sceneFilter, L"srscene", nullptr, scenesDirectory);
        if (chosen)
            m_projectSession.NewScene(*chosen);
        break;
    case ID_FILE_OPENSCENE:
        if (!needsProject() || !confirmDiscardChanges())
            break;
        chosen = showFileDialog(m_hWnd, false, sceneFilter, L"srscene", nullptr, scenesDirectory);
        if (chosen)
            m_projectSession.OpenScene(*chosen);
        break;
    case ID_FILE_SAVESCENE:
        if (needsProject())
            m_projectSession.SaveScene();
        break;
    case ID_FILE_SAVESCENEAS:
        if (!needsProject())
            break;
        chosen = showFileDialog(m_hWnd, true, sceneFilter, L"srscene", nullptr, scenesDirectory);
        if (chosen)
            m_projectSession.SaveSceneAs(*chosen);
        break;
    default:
        return false;
    }
    // 모달 대화상자 동안의 WM_KEYUP은 이 창에 오지 않는다. 눌린 채 남은 키(Ctrl 등)를 지운다.
    m_keys.fill(false);
    return true;
}

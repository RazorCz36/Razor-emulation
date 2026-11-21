#include "mainDialog.h"
#include "launcherTheme.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

#ifdef _WIN32
void EnableWindowsDarkTitleBar(WId win_id) {
	HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
	if (dwm == nullptr) {
		return;
	}
	using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
	// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
	auto* set_attr = reinterpret_cast<DwmSetWindowAttributeFn>(
	    GetProcAddress(dwm, "DwmSetWindowAttribute"));
	if (set_attr != nullptr) {
		const BOOL dark = TRUE;
		// DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 11 / Windows 10 20H1+)
		// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr)
		auto* hwnd = reinterpret_cast<HWND>(win_id);
		set_attr(hwnd, 20u, &dark, sizeof(dark));
		// DWMWA_CAPTION_COLOR = 35 (Windows 11 Build 22000+): #12151A in COLORREF (0x00BBGGRR)
		const COLORREF gunmetal_caption = RGB(0x12, 0x15, 0x1A);
		set_attr(hwnd, 35u, &gunmetal_caption, sizeof(gunmetal_caption));
	}
	FreeLibrary(dwm);
}
#endif

} // namespace

int main(int argc, char* argv[]) {
	QApplication a(argc, argv);
	QCoreApplication::setOrganizationName(QStringLiteral("RazorEmulations"));
	QCoreApplication::setApplicationName(QStringLiteral("RazorEmulations"));
	QGuiApplication::setApplicationDisplayName(QStringLiteral("RazorEmulations"));

	// Title bars and taskbars get the bolder small-size artwork.
	const QIcon small_art(QStringLiteral(":/icons/kyty-small.svg"));
	const QIcon large_art(QStringLiteral(":/icons/kyty.svg"));
	QIcon       icon;
	for (int size: {16, 24, 32, 48, 64}) {
		icon.addPixmap(small_art.pixmap(QSize(size, size), 1.0));
	}
	for (int size: {128, 256}) {
		icon.addPixmap(large_art.pixmap(QSize(size, size), 1.0));
	}
	QApplication::setWindowIcon(icon);
	LauncherTheme::Initialize(a);

	MainDialog w;

	w.emit Start();

	w.show();
#ifdef _WIN32
	EnableWindowsDarkTitleBar(w.winId());
#endif

	return QApplication::exec();
}

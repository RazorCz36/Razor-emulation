#ifndef LAUNCHER_THEME_H
#define LAUNCHER_THEME_H

class QApplication;

namespace LauncherTheme {

enum class Preset {
	GunmetalLuxury = 0,
	ObsidianDark   = 1,
	TitaniumLight  = 2,
	System         = 3,
};

void Initialize(QApplication& application);
void ApplyPreset(QApplication& application, Preset preset);

} // namespace LauncherTheme

#endif // LAUNCHER_THEME_H

#include "launcherTheme.h"

#include <QApplication>
#include <QColor>
#include <QEvent>
#include <QFile>
#include <QObject>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>

namespace {

class PaletteChangeFilter final: public QObject {
public:
	explicit PaletteChangeFilter(QApplication& application)
	    : QObject(&application), m_application(application) {}

protected:
	bool eventFilter(QObject* watched, QEvent* event) override {
		if (event->type() == QEvent::ApplicationPaletteChange && !m_refresh_pending) {
			m_refresh_pending = true;
			QTimer::singleShot(0, &m_application, [this] {
				m_application.setStyleSheet(m_application.styleSheet());
				m_refresh_pending = false;
			});
		}

		return QObject::eventFilter(watched, event);
	}

private:
	QApplication& m_application;
	bool          m_refresh_pending = false;
};

QPalette MakeGunmetalLuxuryPalette(bool deeper_obsidian) {
	QPalette p;
	const QColor window_bg   = deeper_obsidian ? QColor(0x11, 0x14, 0x18) : QColor(0x18, 0x1C, 0x22);
	const QColor recessed_bg = deeper_obsidian ? QColor(0x0B, 0x0D, 0x11) : QColor(0x12, 0x15, 0x1A);
	const QColor alt_bg      = deeper_obsidian ? QColor(0x15, 0x19, 0x1F) : QColor(0x1D, 0x22, 0x2A);
	const QColor button_bg   = deeper_obsidian ? QColor(0x1D, 0x22, 0x2B) : QColor(0x23, 0x29, 0x33);
	const QColor midlight    = QColor(0x2E, 0x36, 0x43);
	const QColor mid_border  = QColor(0x36, 0x3F, 0x4E);
	const QColor dark_shade  = QColor(0x0C, 0x0E, 0x12);
	const QColor text_main   = QColor(0xEC, 0xF0, 0xF6);
	const QColor text_muted  = QColor(0x8B, 0x96, 0xA8);
	const QColor champagne   = QColor(0xC8, 0xA2, 0x56);
	const QColor champagne_hi = QColor(0xDF, 0xB9, 0x6D);
	const QColor on_accent   = QColor(0x10, 0x12, 0x16);

	p.setColor(QPalette::Window, window_bg);
	p.setColor(QPalette::WindowText, text_main);
	p.setColor(QPalette::Base, recessed_bg);
	p.setColor(QPalette::AlternateBase, alt_bg);
	p.setColor(QPalette::ToolTipBase, QColor(0x1E, 0x23, 0x2C));
	p.setColor(QPalette::ToolTipText, text_main);
	p.setColor(QPalette::PlaceholderText, text_muted);
	p.setColor(QPalette::Text, text_main);
	p.setColor(QPalette::Button, button_bg);
	p.setColor(QPalette::ButtonText, text_main);
	p.setColor(QPalette::BrightText, QColor(0xFF, 0xFF, 0xFF));
	p.setColor(QPalette::Light, QColor(0x3B, 0x44, 0x54));
	p.setColor(QPalette::Midlight, midlight);
	p.setColor(QPalette::Mid, mid_border);
	p.setColor(QPalette::Dark, dark_shade);
	p.setColor(QPalette::Shadow, QColor(0x06, 0x07, 0x09));
	p.setColor(QPalette::Highlight, champagne);
	p.setColor(QPalette::HighlightedText, on_accent);
	p.setColor(QPalette::Link, champagne_hi);
	p.setColor(QPalette::LinkVisited, champagne);

	p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(0x63, 0x6C, 0x7C));
	p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x63, 0x6C, 0x7C));
	p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x63, 0x6C, 0x7C));
	p.setColor(QPalette::Disabled, QPalette::Button, QColor(0x1A, 0x1E, 0x26));
	p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x3A, 0x41, 0x4E));
	p.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor(0x88, 0x92, 0xA2));
	return p;
}

QPalette MakeTitaniumLightPalette() {
	QPalette p;
	p.setColor(QPalette::Window, QColor(0xEB, 0xEE, 0xF3));
	p.setColor(QPalette::WindowText, QColor(0x16, 0x1A, 0x21));
	p.setColor(QPalette::Base, QColor(0xF7, 0xF9, 0xFC));
	p.setColor(QPalette::AlternateBase, QColor(0xE2, 0xE7, 0xEF));
	p.setColor(QPalette::ToolTipBase, QColor(0x1C, 0x20, 0x28));
	p.setColor(QPalette::ToolTipText, QColor(0xF4, 0xF6, 0xFA));
	p.setColor(QPalette::PlaceholderText, QColor(0x68, 0x73, 0x85));
	p.setColor(QPalette::Text, QColor(0x16, 0x1A, 0x21));
	p.setColor(QPalette::Button, QColor(0xDF, 0xE4, 0xEC));
	p.setColor(QPalette::ButtonText, QColor(0x16, 0x1A, 0x21));
	p.setColor(QPalette::Midlight, QColor(0xD2, 0xD9, 0xE4));
	p.setColor(QPalette::Mid, QColor(0xB8, 0xC1, 0xD0));
	p.setColor(QPalette::Dark, QColor(0x8C, 0x96, 0xA6));
	p.setColor(QPalette::Highlight, QColor(0x9E, 0x7B, 0x34));
	p.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
	p.setColor(QPalette::Link, QColor(0x85, 0x64, 0x22));
	return p;
}

} // namespace

namespace LauncherTheme {

void Initialize(QApplication& application) {
	QFile style_sheet_file(QStringLiteral(":/styles/launcher.qss"));
	if (style_sheet_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		application.setStyleSheet(QString::fromUtf8(style_sheet_file.readAll()));
		application.installEventFilter(new PaletteChangeFilter(application));
	}
}

void ApplyPreset(QApplication& application, Preset preset) {
	if (preset == Preset::System) {
		QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Unknown);
		application.setPalette(QPalette());
		application.setStyleSheet(application.styleSheet());
		return;
	}

	if (auto* fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
		application.setStyle(fusion);
	}

	switch (preset) {
		case Preset::GunmetalLuxury:
			QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
			application.setPalette(MakeGunmetalLuxuryPalette(false));
			break;
		case Preset::ObsidianDark:
			QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
			application.setPalette(MakeGunmetalLuxuryPalette(true));
			break;
		case Preset::TitaniumLight:
			QApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
			application.setPalette(MakeTitaniumLightPalette());
			break;
		case Preset::System:
			break;
	}
	application.setStyleSheet(application.styleSheet());
}

} // namespace LauncherTheme

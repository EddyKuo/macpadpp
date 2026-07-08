#pragma once

// StyleConfiguratorDialog — 複刻 Notepad++ Style Configurator。
// 可設定全域編輯字型，以及「每語言、每語法 style」的前景/背景/粗體/斜體覆寫，存入 styles.json。
// 按下 OK 後 MainWindow 重新套用主題即生效（覆寫優先於自動降飽和）。

#include <QDialog>

#include "persistence/StyleStore.h"

class QComboBox;
class QFontComboBox;
class QSpinBox;
class QListWidget;
class QCheckBox;
class QLabel;
class QPushButton;
class QLineEdit;
class QsciLexer;

namespace macpad::ui {

class StyleConfiguratorDialog : public QDialog {
    Q_OBJECT
public:
    explicit StyleConfiguratorDialog(QWidget *parent = nullptr);
    ~StyleConfiguratorDialog() override;

private slots:
    void onLanguageChanged();
    void onStyleChanged();
    void pickForeground();
    void pickBackground();
    void onBoldItalicChanged();
    void onExtensionsEdited();
    void pickGlobalFg();
    void pickGlobalBg();
    void onGlobalOverrideToggled();
    void apply();

private:
    macpad::persistence::StyleOverride *currentOverride(bool create);
    void refreshSwatches();
    void refreshGlobalSwatch();
    void refreshGlobalOverrideSwatches();

    macpad::persistence::StyleSettings m_cfg;
    QString m_langKey;          // 目前語言的 LexerFactory key；等於 kGlobalStylesKey 時代表選中 Global Styles
    QString m_langName;         // 目前 lexer 的 language()（styles.json 的鍵）
    QsciLexer *m_lexer = nullptr;   // 暫時 lexer（取 style 名稱與預設色）；Global Styles 模式下為 nullptr

    QFontComboBox *m_font = nullptr;
    QSpinBox *m_size = nullptr;
    QComboBox *m_language = nullptr;
    QListWidget *m_styles = nullptr;
    QLabel *m_fgSwatch = nullptr;
    QLabel *m_bgSwatch = nullptr;
    QPushButton *m_fgBtn = nullptr;
    QPushButton *m_bgBtn = nullptr;
    QCheckBox *m_bold = nullptr;
    QCheckBox *m_italic = nullptr;
    QCheckBox *m_underline = nullptr;

    QLineEdit *m_userExt = nullptr;  // 每語言「User ext.」自訂副檔名輸入框（Global Styles 模式下停用）

    // Global override（單一 fg/bg 套用到所有語言）
    QCheckBox *m_enableGlobalFg = nullptr;
    QLabel *m_globalFgSwatch = nullptr;
    QPushButton *m_globalFgBtn = nullptr;
    QCheckBox *m_enableGlobalBg = nullptr;
    QLabel *m_globalBgSwatch = nullptr;
    QPushButton *m_globalBgBtn = nullptr;
};

}  // namespace macpad::ui

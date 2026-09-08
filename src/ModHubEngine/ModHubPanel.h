// ModHubPanel.h - 引擎内模组启动面板声明
// 依赖 Source SDK（vgui_controls / filesystem / engine 接口）

#pragma once

#include "vgui_controls/Frame.h"

class ModHubPanel : public vgui::Frame {
    typedef vgui::Frame BaseClass;

public:
    ModHubPanel(vgui::Panel* parent, const char* name);

    void RefreshList();  // 重新扫描模组文件夹
    virtual void OnCommand(const char* command) override;

private:
    vgui::ListPanel* m_pList = nullptr;
    vgui::Button* m_pLaunch = nullptr;
    vgui::Button* m_pClose = nullptr;
    vgui::Label* m_pStatus = nullptr;
};

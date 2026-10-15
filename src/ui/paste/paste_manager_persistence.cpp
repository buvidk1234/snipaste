#include "core/paste/paste_state_store.h"
#include "ui/paste/paste_manager.h"
#include "ui/paste/paste_window.h"
namespace snipaste::ui
{
void PasteManager::loadState(const QString &directory)
{
    const auto state = core::PasteStateStore::load(directory);
    if (!state)
        return;
    destroyAll();
    m_groups.clear();
    for (const auto &savedGroup : state->groups)
    {
        Group group{savedGroup.name, {}};
        for (const auto &pin : savedGroup.windows)
        {
            auto *window = new PasteWindow(pin.image);
            applyWindowSettings(window);
            window->setActivateOnShow(true);
            window->restore(pin);
            group.windows.append(window);
            connectWindow(window);
            if (pin.options.value(QStringLiteral("visible")).toBool(true))
                window->showExisting();
            else
                window->hide();
        }
        m_groups.append(std::move(group));
    }
    m_activeGroup = state->activeGroup;
}
void PasteManager::saveState(const QString &directory) const
{
    core::PasteSessionState state;
    state.activeGroup = m_activeGroup;
    QList<PasteWindow *> windows;
    for (const auto &group : m_groups)
    {
        core::PasteGroupState savedGroup{group.name, {}};
        for (const auto &window : group.windows)
        {
            if (!window || window->state() != PasteWindow::State::Active || window->sourceImage().isNull())
                continue;
            savedGroup.windows.append(window->snapshot());
            windows.append(window.data());
        }
        state.groups.append(std::move(savedGroup));
    }
    if (!core::PasteStateStore::save(state, directory))
        return;
    qsizetype index = 0;
    for (const auto &group : state.groups)
    {
        for (const auto &pin : group.windows)
        {
            windows[index]->m_savedImageKey = pin.savedImageKey;
            windows[index]->m_savedImagePath = pin.savedImagePath;
            ++index;
        }
    }
}
} // namespace snipaste::ui

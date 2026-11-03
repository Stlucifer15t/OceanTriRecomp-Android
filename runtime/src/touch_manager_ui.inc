// Included inside TouchSettingsEngine. Touch/GL-thread UI only: every state
// change is queued to the game thread (or, for texture roots, the GL thread).
// No guest memory is read or written here.

    struct ManagerLayout {
        float topY = 0, catY = 0, searchY = 0, listY = 0, footerY = 0;
        float listH = 0, rowH = 64;
        int visible = 1;
    };

    CheatManager::Snapshot m_cheatSnapshot{};
    CustomCodes::View m_customView{};
    ModManager::View m_modView{};
    int m_managerCategory = 0; // Player, Items, Combat, Progression, Advanced, Gecko
    int m_modFilter = 0;       // All, Active, Textures, Files, Issues
    int m_managerPage = 0;
    std::string m_managerSearch;
    bool m_searchActive = false;
    int64_t m_managerRefreshMs = 0;
    std::string m_managerMessage;
    bool m_confirmOpen = false;
    int m_confirmKind = 0; // 0 master, 1 native, 2 custom
    size_t m_confirmNative = 0;
    uint64_t m_confirmId = 0;
    bool m_confirmValue = true;

    static std::string ManagerLower(std::string s) {
        for (auto& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    static bool ManagerContains(std::string_view haystack, std::string_view needle) {
        return needle.empty() || ManagerLower(std::string(haystack)).find(ManagerLower(std::string(needle))) != std::string::npos;
    }

    std::string ManagerFit(const std::string& text, float width, float size) {
        if (MeasureText(text.c_str(), size) <= width) return text;
        std::string out = text;
        while (!out.empty() && MeasureText((out + "...").c_str(), size) > width) {
            // Remove one UTF-8 code point, not one continuation byte.
            size_t n = out.size();
            do { --n; } while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80);
            out.resize(n);
        }
        return out + "...";
    }

    ManagerLayout ManagerGetLayout(float y, float w) const {
        (void)w;
        ManagerLayout l;
        const float bottom = float(m_height) * 0.96f - 18.0f;
        l.topY = y;
        l.catY = y + 52.0f;
        l.searchY = l.catY + 46.0f;
        l.listY = l.searchY + 50.0f;
        l.footerY = bottom - 46.0f;
        l.listH = std::max(64.0f, l.footerY - l.listY - 10.0f);
        l.visible = std::max(1, std::min(6, int(l.listH / 64.0f)));
        l.rowH = (l.listH - float(l.visible - 1) * 6.0f) / float(l.visible);
        return l;
    }

    float ManagerContentY() const { return float(m_height) * 0.04f + 86.0f + 44.0f + 16.0f; }
    float ManagerContentX() const { return float(m_width) * 0.05f + 20.0f; }
    float ManagerContentW() const { return float(m_width) * 0.90f - 40.0f; }

    void ManagerPageReset() { m_managerPage = 0; }
    void ManagerPage(int delta) { m_managerPage = std::max(0, m_managerPage + delta); m_managerRefreshMs = 0; }

    void ManagerStopSearch() {
        if (m_searchActive) {
            if (SDL_Window* win = aurora_get_window()) SDL_StopTextInput(win);
            m_searchActive = false;
        }
    }

    void ManagerStartSearch() {
        m_searchActive = true;
        if (SDL_Window* win = aurora_get_window()) {
            SDL_SetTextInputArea(win, nullptr, 0);
            SDL_StartTextInput(win);
        }
    }

public:
    bool HandleManagerSdlEvent(const SDL_Event* event) {
        if (!m_menuOpen || m_menuTab < 5 || !m_searchActive || !event) return false;
        if (event->type == SDL_EVENT_TEXT_INPUT && event->text.text) {
            for (const char* p = event->text.text; *p; ++p) {
                if (m_managerSearch.size() < 64) m_managerSearch.push_back(*p);
            }
            ManagerPageReset();
            m_managerRefreshMs = 0;
            return true;
        }
        if (event->type == SDL_EVENT_KEY_DOWN) {
            const auto key = event->key.key;
            if (key == SDLK_BACKSPACE || key == SDLK_DELETE) {
                if (!m_managerSearch.empty()) {
                    size_t n = m_managerSearch.size();
                    do { --n; } while (n > 0 && (static_cast<unsigned char>(m_managerSearch[n]) & 0xC0) == 0x80);
                    m_managerSearch.resize(n);
                }
                ManagerPageReset();
                m_managerRefreshMs = 0;
                return true;
            }
            if (key == SDLK_RETURN || key == SDLK_KP_ENTER || key == SDLK_ESCAPE || key == SDLK_AC_BACK) {
                ManagerStopSearch();
                return true;
            }
        }
        return false;
    }

private:
    void RefreshManagerState() {
        const auto now = NowMs();
        if (now - m_managerRefreshMs < 200) return;
        m_managerRefreshMs = now;
        m_cheatSnapshot = CheatManager::GetSnapshot();
        m_customView = CustomCodes::GetView();
        m_modView = ModManager::GetView();
    }

    std::vector<const CheatManager::Entry*> FilteredCheats() const {
        std::vector<const CheatManager::Entry*> rows;
        if (m_managerCategory == 5) return rows;
        for (const auto& entry : CheatManager::Catalog()) {
            if (int(entry.category) != m_managerCategory) continue;
            if (!ManagerContains(entry.title, m_managerSearch) && !ManagerContains(entry.detail, m_managerSearch)) continue;
            rows.push_back(&entry);
        }
        return rows;
    }

    std::vector<size_t> FilteredCustom() const {
        std::vector<size_t> rows;
        for (size_t i = 0; i < m_customView.codes.size(); ++i) {
            const auto& code = m_customView.codes[i];
            if (ManagerContains(code.name, m_managerSearch) || ManagerContains(code.sourceGame, m_managerSearch)) rows.push_back(i);
        }
        return rows;
    }

    std::vector<size_t> FilteredMods() const {
        std::vector<size_t> rows;
        for (size_t i = 0; i < m_modView.packages.size(); ++i) {
            const auto& p = m_modView.packages[i];
            if (m_modFilter == 1 && !p.active) continue;
            if (m_modFilter == 2 && p.textureRoot.empty()) continue;
            if (m_modFilter == 3 && p.entries.empty()) continue;
            if (m_modFilter == 4 && p.valid && !p.conflict && p.error.empty()) continue;
            if (!ManagerContains(p.name, m_managerSearch) && !ManagerContains(p.id, m_managerSearch) && !ManagerContains(p.description, m_managerSearch)) continue;
            rows.push_back(i);
        }
        return rows;
    }

    int ManagerRowCount() const {
        if (m_menuTab == 6) return int(FilteredMods().size());
        return m_managerCategory == 5 ? int(FilteredCustom().size()) : int(FilteredCheats().size());
    }

    void ManagerClampPage(const ManagerLayout& l) {
        const int count = ManagerRowCount();
        const int pages = std::max(1, (count + l.visible - 1) / l.visible);
        m_managerPage = std::clamp(m_managerPage, 0, pages - 1);
    }

    std::string NativeValueLabel(size_t native, uint32_t selected) const {
        const auto list = LauncherCheats::List();
        if (native >= list.size()) return {};
        const auto& cheat = list[native];
        if (!cheat.options.empty() && selected < cheat.options.size()) return cheat.options[selected].label;
        if (cheat.range.max > cheat.range.min) return std::to_string(cheat.range.min + int(selected) * cheat.range.step);
        return {};
    }

    bool ManagerRiskSelected() const {
        for (const auto& entry : CheatManager::Catalog())
            if (entry.saveRisk && entry.native < m_cheatSnapshot.count && m_cheatSnapshot.enabled[entry.native]) return true;
        for (const auto enabled : m_customView.enabled) if (enabled) return true;
        return false;
    }

    void ManagerDrawButton(float x, float y, float w, float h, const char* label, bool active, bool enabled = true, float fontSize = 14.0f) {
        DrawRoundedRect(x, y, w, h, 8.0f,
                        enabled ? (active ? COL_ACTIVE_CHIP : COL_INACTIVE_CHIP) : Color{0.05f, 0.08f, 0.10f, 0.7f},
                        enabled ? (active ? COL_CYAN : COL_CHIP_BORDER) : COL_PANEL_BORDER);
        DrawButtonLabel(x + w * 0.5f, y + h * 0.5f, label, fontSize, enabled ? (active ? COL_ACTIVE_TEXT : COL_WHITE) : COL_MUTED);
    }

    static bool ManagerIn(float px, float py, float x, float y, float w, float h) {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }

    void ManagerQueueFailed() { m_managerMessage = Tr("Cola llena; inténtalo de nuevo.", "Command queue full; try again."); m_managerRefreshMs = 0; }

    void ManagerRequestToggle(size_t native, bool value, bool saveRisk) {
        if (value && saveRisk) {
            m_confirmOpen = true; m_confirmKind = 1; m_confirmNative = native; m_confirmValue = true;
            return;
        }
        if (!CheatManager::Queue(CheatManager::Action::Toggle, native, value ? 1 : 0)) ManagerQueueFailed();
        m_managerRefreshMs = 0;
    }

    void ManagerRequestCustom(uint64_t id, bool value) {
        if (value) {
            m_confirmOpen = true; m_confirmKind = 2; m_confirmId = id; m_confirmValue = true;
            return;
        }
        if (!CustomCodes::QueueToggle(id, false)) ManagerQueueFailed();
        m_managerRefreshMs = 0;
    }

    void ManagerRequestMaster(bool value) {
        if (value && ManagerRiskSelected()) {
            m_confirmOpen = true; m_confirmKind = 0; m_confirmValue = true;
            return;
        }
        if (!CheatManager::Queue(CheatManager::Action::Master, 0, value ? 1 : 0)) ManagerQueueFailed();
        m_managerRefreshMs = 0;
    }

    void ManagerExecuteConfirm() {
        if (m_confirmKind == 0) {
            if (!CheatManager::Queue(CheatManager::Action::Master, 0, m_confirmValue ? 1 : 0)) ManagerQueueFailed();
        } else if (m_confirmKind == 1) {
            if (!CheatManager::Queue(CheatManager::Action::Toggle, m_confirmNative, m_confirmValue ? 1 : 0)) ManagerQueueFailed();
        } else {
            if (!CustomCodes::QueueToggle(m_confirmId, m_confirmValue)) ManagerQueueFailed();
        }
        m_confirmOpen = false;
        m_managerRefreshMs = 0;
    }

    void RenderManagerConfirm(float padX, float padY, float panW, float panH) {
        DrawRectSolid(0, 0, float(m_width), float(m_height), {0, 0, 0, 0.55f});
        const float w = std::min(560.0f, panW - 40.0f);
        const float h = 190.0f;
        const float x = padX + (panW - w) * 0.5f;
        const float y = padY + (panH - h) * 0.5f;
        DrawRoundedRect(x, y, w, h, 14.0f, COL_BG_DARK, COL_CYAN);
        DrawText(x + 20.0f, y + 18.0f, Tr("Copia de seguridad primero", "Back up saves first"), 20.0f, COL_WHITE);
        DrawText(x + 20.0f, y + 56.0f, ManagerFit(Tr("Este cambio puede guardarse y persistir al apagar el truco.", "This change can be saved and persist after the cheat is OFF."), w - 40.0f, 14.0f).c_str(), 14.0f, COL_MUTED);
        DrawText(x + 20.0f, y + 82.0f, ManagerFit(Tr("Los desbloqueos de una vez no se revierten. El juego online requiere reiniciar.", "One-shot unlocks are not reverted. Online play needs a fresh launch."), w - 40.0f, 14.0f).c_str(), 14.0f, COL_MUTED);
        const float bw = (w - 60.0f) * 0.5f;
        ManagerDrawButton(x + 20.0f, y + h - 64.0f, bw, 44.0f, Tr("Cancelar", "Cancel"), false);
        ManagerDrawButton(x + 40.0f + bw, y + h - 64.0f, bw, 44.0f, Tr("Activar", "Enable"), true);
    }

    void HandleManagerConfirmTouch(float px, float py) {
        const float padX = float(m_width) * 0.05f, padY = float(m_height) * 0.04f;
        const float panW = float(m_width) - padX * 2.0f, panH = float(m_height) - padY * 2.0f;
        const float w = std::min(560.0f, panW - 40.0f), h = 190.0f;
        const float x = padX + (panW - w) * 0.5f, y = padY + (panH - h) * 0.5f;
        const float bw = (w - 60.0f) * 0.5f;
        if (ManagerIn(px, py, x + 20.0f, y + h - 64.0f, bw, 44.0f)) { m_confirmOpen = false; return; }
        if (ManagerIn(px, py, x + 40.0f + bw, y + h - 64.0f, bw, 44.0f)) { ManagerExecuteConfirm(); return; }
    }

    void RenderManagerSearch(float x, float y, float w) {
        const float fieldW = w - 54.0f;
        DrawRoundedRect(x, y, fieldW, 42.0f, 8.0f, COL_INACTIVE_CHIP, m_searchActive ? COL_CYAN : COL_CHIP_BORDER);
        const auto text = m_managerSearch.empty() ? Tr("Buscar… (tocar para teclado)", "Search… (tap for keyboard)") : m_managerSearch;
        DrawText(x + 12.0f, y + 12.0f, ManagerFit(text, fieldW - 24.0f, 14.0f).c_str(), 14.0f, m_managerSearch.empty() ? COL_MUTED : COL_WHITE);
        ManagerDrawButton(x + fieldW + 6.0f, y, 48.0f, 42.0f, "✕", false);
    }

    void RenderManagerFooter(float x, float y, float w, bool cheats) {
        const float gap = 8.0f;
        const float bw = (w - 4.0f * gap) / 5.0f;
        const int count = ManagerRowCount();
        const auto l = ManagerGetLayout(ManagerContentY(), w);
        const int pages = std::max(1, (count + l.visible - 1) / l.visible);
        char page[48];
        std::snprintf(page, sizeof(page), "%d/%d", m_managerPage + 1, pages);
        if (cheats) {
            ManagerDrawButton(x, y, bw, 46.0f, m_managerCategory == 5 ? Tr("Reescanear", "Rescan") : Tr("Importar", "Import"), false);
            ManagerDrawButton(x + (bw + gap), y, bw, 46.0f, Tr("Exportar", "Export"), false);
            ManagerDrawButton(x + 2.0f * (bw + gap), y, bw, 46.0f, Tr("Repetir", "Repeat"), false);
            ManagerDrawButton(x + 3.0f * (bw + gap), y, bw, 46.0f, "◀", false, m_managerPage > 0);
            ManagerDrawButton(x + 4.0f * (bw + gap), y, bw, 46.0f, "▶", false, m_managerPage + 1 < pages);
        } else {
            const float mw = (w - 2.0f * gap) / 3.0f;
            ManagerDrawButton(x, y, mw, 46.0f, Tr("Reescanear paquetes", "Rescan packages"), false);
            ManagerDrawButton(x + (mw + gap), y, mw, 46.0f, "◀", false, m_managerPage > 0);
            ManagerDrawButton(x + 2.0f * (mw + gap), y, mw, 46.0f, "▶", false, m_managerPage + 1 < pages);
        }
        DrawText(x + w - 4.0f, y - 18.0f, page, 12.0f, COL_MUTED, true);
    }

    void RenderTabCheats(float x, float y, float w) {
        const auto l = ManagerGetLayout(y, w);
        ManagerClampPage(l);
        const bool master = m_cheatSnapshot.master;
        ManagerDrawButton(x, l.topY, 150.0f, 44.0f, master ? Tr("Maestro: ON", "Master: ON") : Tr("Maestro: OFF", "Master: OFF"), master);
        ManagerDrawButton(x + 158.0f, l.topY, 104.0f, 44.0f, Tr("Reset", "Reset"), false);
        ManagerDrawButton(x + 270.0f, l.topY, 110.0f, 44.0f, Tr("Todo OFF", "All OFF"), false);
        const auto status = m_managerMessage.empty() ? m_cheatSnapshot.status : m_managerMessage;
        DrawText(x + 392.0f, l.topY + 14.0f, ManagerFit(status, std::max(40.0f, w - 392.0f), 13.0f).c_str(), 13.0f, m_cheatSnapshot.used ? COL_CYAN : COL_MUTED);

        const char* cats[] = {Tr("Jugador", "Player"), Tr("Objetos", "Items"), Tr("Combate", "Combat"), Tr("Progreso", "Progress"), Tr("Avanzado", "Advanced"), "Gecko"};
        const float cw = (w - 5.0f * 6.0f) / 6.0f;
        for (int i = 0; i < 6; ++i)
            ManagerDrawButton(x + float(i) * (cw + 6.0f), l.catY, cw, 38.0f, cats[i], m_managerCategory == i, true, 13.0f);
        RenderManagerSearch(x, l.searchY, w);

        if (m_managerCategory == 5) {
            const auto rows = FilteredCustom();
            if (rows.empty()) {
                DrawText(x + 12.0f, l.listY + 12.0f, ManagerFit(Tr("Sin códigos. Coloca .txt + .map en MH3/cheats/gecko y reescanea.", "No codes. Put .txt + .map in MH3/cheats/gecko and rescan."), w - 24.0f, 14.0f).c_str(), 14.0f, COL_MUTED);
                DrawText(x + 12.0f, l.listY + 40.0f, ManagerFit(m_customView.status, w - 24.0f, 12.5f).c_str(), 12.5f, COL_MUTED);
            }
            for (int r = 0; r < l.visible; ++r) {
                const int index = m_managerPage * l.visible + r;
                if (index >= int(rows.size())) break;
                const auto& code = m_customView.codes[rows[size_t(index)]];
                const bool on = rows[size_t(index)] < m_customView.enabled.size() && m_customView.enabled[rows[size_t(index)]];
                const float ry = l.listY + float(r) * (l.rowH + 6.0f);
                DrawRoundedRect(x, ry, w, l.rowH, 10.0f, COL_PANEL_CARD, on ? COL_CYAN : COL_PANEL_BORDER);
                DrawText(x + 14.0f, ry + 10.0f, ManagerFit(code.name, w - 130.0f, 16.0f).c_str(), 16.0f, COL_WHITE);
                char detail[192];
                std::snprintf(detail, sizeof(detail), "%s • %zu %s • %s", code.sourceGame.c_str(), code.instructions.size(), Tr("escrituras mapeadas", "mapped writes"), Tr("OFF no restaura RAM", "OFF does not restore RAM"));
                DrawText(x + 14.0f, ry + 36.0f, ManagerFit(detail, w - 130.0f, 12.0f).c_str(), 12.0f, COL_MUTED);
                ManagerDrawButton(x + w - 90.0f, ry + (l.rowH - 40.0f) * 0.5f, 76.0f, 40.0f, on ? "ON" : "OFF", on);
            }
        } else {
            const auto rows = FilteredCheats();
            if (rows.empty()) DrawText(x + 12.0f, l.listY + 12.0f, Tr("Sin resultados.", "No matches."), 15.0f, COL_MUTED);
            for (int r = 0; r < l.visible; ++r) {
                const int index = m_managerPage * l.visible + r;
                if (index >= int(rows.size())) break;
                const auto& entry = *rows[size_t(index)];
                const bool on = entry.native < m_cheatSnapshot.count && m_cheatSnapshot.enabled[entry.native];
                const size_t choices = LauncherCheats::Choices(entry.native);
                const float ry = l.listY + float(r) * (l.rowH + 6.0f);
                DrawRoundedRect(x, ry, w, l.rowH, 10.0f, COL_PANEL_CARD, on ? COL_CYAN : COL_PANEL_BORDER);
                const float textW = w - (choices ? 230.0f : 130.0f);
                DrawText(x + 14.0f, ry + 10.0f, ManagerFit(std::string(entry.title), textW, 16.0f).c_str(), 16.0f, COL_WHITE);
                std::string detail(entry.detail);
                if (entry.saveRisk) detail = std::string(Tr("RIESGO DE GUARDADO • ", "SAVE RISK • ")) + detail;
                if (choices) {
                    const uint32_t selected = entry.native < m_cheatSnapshot.count ? m_cheatSnapshot.selected[entry.native] : 0;
                    detail = NativeValueLabel(entry.native, selected) + " • " + detail;
                }
                DrawText(x + 14.0f, ry + 36.0f, ManagerFit(detail, textW, 12.0f).c_str(), 12.0f, entry.saveRisk ? COL_CYAN : COL_MUTED);
                if (choices) {
                    ManagerDrawButton(x + w - 174.0f, ry + (l.rowH - 36.0f) * 0.5f, 36.0f, 36.0f, "−", false);
                    ManagerDrawButton(x + w - 134.0f, ry + (l.rowH - 36.0f) * 0.5f, 36.0f, 36.0f, "+", false);
                }
                ManagerDrawButton(x + w - 90.0f, ry + (l.rowH - 40.0f) * 0.5f, 76.0f, 40.0f, on ? "ON" : "OFF", on);
            }
        }
        RenderManagerFooter(x, l.footerY, w, true);
    }

    void RenderTabMods(float x, float y, float w) {
        const auto l = ManagerGetLayout(y, w);
        ManagerClampPage(l);
        ManagerDrawButton(x, l.topY, 150.0f, 44.0f, Tr("Reescanear", "Rescan"), false);
        DrawText(x + 162.0f, l.topY + 14.0f, ManagerFit(m_modView.status, std::max(40.0f, w - 162.0f), 13.0f).c_str(), 13.0f, COL_MUTED);
        const char* filters[] = {Tr("Todos", "All"), Tr("Activos", "Active"), Tr("Texturas", "Textures"), Tr("Archivos", "Files"), Tr("Problemas", "Issues")};
        const float cw = (w - 4.0f * 6.0f) / 5.0f;
        for (int i = 0; i < 5; ++i)
            ManagerDrawButton(x + float(i) * (cw + 6.0f), l.catY, cw, 38.0f, filters[i], m_modFilter == i, true, 13.0f);
        RenderManagerSearch(x, l.searchY, w);
        const auto rows = FilteredMods();
        if (rows.empty()) {
            DrawText(x + 12.0f, l.listY + 12.0f, ManagerFit(Tr("Sin paquetes. Coloca carpetas con package.oceantri en MH3/mods y reescanea.", "No packages. Put folders with package.oceantri in MH3/mods and rescan."), w - 24.0f, 14.0f).c_str(), 14.0f, COL_MUTED);
        }
        for (int r = 0; r < l.visible; ++r) {
            const int index = m_managerPage * l.visible + r;
            if (index >= int(rows.size())) break;
            const auto& p = m_modView.packages[rows[size_t(index)]];
            const float ry = l.listY + float(r) * (l.rowH + 6.0f);
            const bool issue = !p.valid || p.conflict || !p.error.empty();
            DrawRoundedRect(x, ry, w, l.rowH, 10.0f, COL_PANEL_CARD, p.active ? COL_CYAN : COL_PANEL_BORDER);
            DrawText(x + 14.0f, ry + 10.0f, ManagerFit(p.name, w - 130.0f, 16.0f).c_str(), 16.0f, issue ? COL_CYAN : COL_WHITE);
            std::string detail = p.id + " • " + std::to_string(p.textureFiles) + " tex • " + std::to_string(p.entries.size()) + " files";
            if (p.filePending) detail += Tr(" • reinicio para archivos", " • restart for files");
            if (!p.error.empty()) detail += " • " + p.error;
            DrawText(x + 14.0f, ry + 36.0f, ManagerFit(detail, w - 130.0f, 12.0f).c_str(), 12.0f, issue ? COL_CYAN : COL_MUTED);
            ManagerDrawButton(x + w - 90.0f, ry + (l.rowH - 40.0f) * 0.5f, 76.0f, 40.0f, p.active ? "ON" : "OFF", p.active, p.valid);
        }
        RenderManagerFooter(x, l.footerY, w, false);
    }

    void HandleCheatsTouch(float px, float py, float x, float y, float w) {
        const auto l = ManagerGetLayout(y, w);
        ManagerClampPage(l);
        if (ManagerIn(px, py, x, l.topY, 150.0f, 44.0f)) { ManagerRequestMaster(!m_cheatSnapshot.master); return; }
        if (ManagerIn(px, py, x + 158.0f, l.topY, 104.0f, 44.0f)) { CheatManager::Queue(CheatManager::Action::ResetLatches); m_managerRefreshMs = 0; return; }
        if (ManagerIn(px, py, x + 270.0f, l.topY, 110.0f, 44.0f)) { CheatManager::Queue(CheatManager::Action::DisableAll); m_managerMessage.clear(); m_managerRefreshMs = 0; return; }
        const float cw = (w - 5.0f * 6.0f) / 6.0f;
        for (int i = 0; i < 6; ++i) {
            if (ManagerIn(px, py, x + float(i) * (cw + 6.0f), l.catY, cw, 38.0f)) {
                m_managerCategory = i; ManagerPageReset(); m_managerMessage.clear(); return;
            }
        }
        const float fieldW = w - 54.0f;
        if (ManagerIn(px, py, x, l.searchY, fieldW, 42.0f)) { ManagerStartSearch(); return; }
        if (ManagerIn(px, py, x + fieldW + 6.0f, l.searchY, 48.0f, 42.0f)) { m_managerSearch.clear(); ManagerStopSearch(); ManagerPageReset(); return; }
        const float gap = 8.0f, bw = (w - 4.0f * gap) / 5.0f;
        if (ManagerIn(px, py, x, l.footerY, bw, 46.0f)) {
            if (m_managerCategory == 5) CheatManager::Queue(CheatManager::Action::RefreshCustom);
            else CheatManager::QueueFile(CheatManager::Action::Import, CheatManager::ImportPath());
            m_managerRefreshMs = 0; return;
        }
        if (ManagerIn(px, py, x + (bw + gap), l.footerY, bw, 46.0f)) { CheatManager::QueueFile(CheatManager::Action::Export, CheatManager::ExportPath()); m_managerRefreshMs = 0; return; }
        if (ManagerIn(px, py, x + 2.0f * (bw + gap), l.footerY, bw, 46.0f)) { CheatManager::Queue(CheatManager::Action::RepeatUnlocks); m_managerRefreshMs = 0; return; }
        if (ManagerIn(px, py, x + 3.0f * (bw + gap), l.footerY, bw, 46.0f)) { ManagerPage(-1); return; }
        if (ManagerIn(px, py, x + 4.0f * (bw + gap), l.footerY, bw, 46.0f)) { ManagerPage(1); return; }

        if (m_managerCategory == 5) {
            const auto rows = FilteredCustom();
            for (int r = 0; r < l.visible; ++r) {
                const int index = m_managerPage * l.visible + r;
                if (index >= int(rows.size())) break;
                const float ry = l.listY + float(r) * (l.rowH + 6.0f);
                if (ManagerIn(px, py, x + w - 90.0f, ry + (l.rowH - 40.0f) * 0.5f, 76.0f, 40.0f)) {
                    const size_t ci = rows[size_t(index)];
                    const bool on = ci < m_customView.enabled.size() && m_customView.enabled[ci];
                    ManagerRequestCustom(m_customView.codes[ci].id, !on);
                    return;
                }
            }
            return;
        }
        const auto rows = FilteredCheats();
        for (int r = 0; r < l.visible; ++r) {
            const int index = m_managerPage * l.visible + r;
            if (index >= int(rows.size())) break;
            const auto& entry = *rows[size_t(index)];
            const float ry = l.listY + float(r) * (l.rowH + 6.0f);
            const size_t choices = LauncherCheats::Choices(entry.native);
            if (choices && ManagerIn(px, py, x + w - 174.0f, ry + (l.rowH - 36.0f) * 0.5f, 36.0f, 36.0f)) {
                const uint32_t selected = entry.native < m_cheatSnapshot.count ? m_cheatSnapshot.selected[entry.native] : 0;
                CheatManager::Queue(CheatManager::Action::Select, entry.native, selected > 0 ? selected - 1 : uint32_t(choices - 1));
                m_managerRefreshMs = 0; return;
            }
            if (choices && ManagerIn(px, py, x + w - 134.0f, ry + (l.rowH - 36.0f) * 0.5f, 36.0f, 36.0f)) {
                const uint32_t selected = entry.native < m_cheatSnapshot.count ? m_cheatSnapshot.selected[entry.native] : 0;
                CheatManager::Queue(CheatManager::Action::Select, entry.native, (selected + 1) % uint32_t(choices));
                m_managerRefreshMs = 0; return;
            }
            if (ManagerIn(px, py, x + w - 90.0f, ry + (l.rowH - 40.0f) * 0.5f, 76.0f, 40.0f)) {
                const bool on = entry.native < m_cheatSnapshot.count && m_cheatSnapshot.enabled[entry.native];
                ManagerRequestToggle(entry.native, !on, entry.saveRisk);
                return;
            }
        }
    }

    void HandleModsTouch(float px, float py, float x, float y, float w) {
        const auto l = ManagerGetLayout(y, w);
        ManagerClampPage(l);
        if (ManagerIn(px, py, x, l.topY, 150.0f, 44.0f)) { ModManager::QueueRefresh(); m_managerRefreshMs = 0; return; }
        const float cw = (w - 4.0f * 6.0f) / 5.0f;
        for (int i = 0; i < 5; ++i) {
            if (ManagerIn(px, py, x + float(i) * (cw + 6.0f), l.catY, cw, 38.0f)) { m_modFilter = i; ManagerPageReset(); return; }
        }
        const float fieldW = w - 54.0f;
        if (ManagerIn(px, py, x, l.searchY, fieldW, 42.0f)) { ManagerStartSearch(); return; }
        if (ManagerIn(px, py, x + fieldW + 6.0f, l.searchY, 48.0f, 42.0f)) { m_managerSearch.clear(); ManagerStopSearch(); ManagerPageReset(); return; }
        const float gap = 8.0f, mw = (w - 2.0f * gap) / 3.0f;
        if (ManagerIn(px, py, x, l.footerY, mw, 46.0f)) { ModManager::QueueRefresh(); m_managerRefreshMs = 0; return; }
        if (ManagerIn(px, py, x + (mw + gap), l.footerY, mw, 46.0f)) { ManagerPage(-1); return; }
        if (ManagerIn(px, py, x + 2.0f * (mw + gap), l.footerY, mw, 46.0f)) { ManagerPage(1); return; }
        const auto rows = FilteredMods();
        for (int r = 0; r < l.visible; ++r) {
            const int index = m_managerPage * l.visible + r;
            if (index >= int(rows.size())) break;
            const auto& p = m_modView.packages[rows[size_t(index)]];
            const float ry = l.listY + float(r) * (l.rowH + 6.0f);
            if (p.valid && ManagerIn(px, py, x + w - 90.0f, ry + (l.rowH - 40.0f) * 0.5f, 76.0f, 40.0f)) {
                if (!ModManager::QueueToggle(p.id, !p.active)) ManagerQueueFailed();
                m_managerRefreshMs = 0;
                return;
            }
        }
    }

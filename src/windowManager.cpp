#include "windowManager.hpp"
#include "./events/events.hpp"

void WindowManager::setupManager() {
    WindowManager::Values[0] = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_change_window_attributes_checked(WindowManager::DisplayConnection, WindowManager::Screen->root,
                                         XCB_CW_EVENT_MASK, WindowManager::Values);
    xcb_ungrab_key(WindowManager::DisplayConnection, XCB_GRAB_ANY, WindowManager::Screen->root, XCB_MOD_MASK_ANY);
    /*int key_table_size = sizeof(keys) / sizeof(*keys);
    for (int i = 0; i < key_table_size; ++i) {
        xcb_keycode_t *keycode = xcb_get_keycodes(keys[i].keysym);
        if (keycode != NULL) {
            xcb_grab_key(WindowManager::DisplayConnection, 1, WindowManager::Screen->root, keys[i].mod, *keycode,
                         XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        }
    }
    xcb_flush(WindowManager::DisplayConnection);
    xcb_grab_button(WindowManager::DisplayConnection, 0, WindowManager::Screen->root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
                    XCB_GRAB_MODE_ASYNC, WindowManager::Screen->root, XCB_NONE, 1, MOD1);
    xcb_grab_button(WindowManager::DisplayConnection, 0, WindowManager::Screen->root, XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
                    XCB_GRAB_MODE_ASYNC, WindowManager::Screen->root, XCB_NONE, 3, MOD1);*/
    xcb_flush(WindowManager::DisplayConnection);
}

bool WindowManager::handleEvent() {
    if (xcb_connection_has_error(WindowManager::DisplayConnection))
        return false;

    xcb_generic_event_t* ev = xcb_wait_for_event(WindowManager::DisplayConnection);
    if (ev != NULL) {
        switch (ev->response_type & ~0x80) {
            case XCB_ENTER_NOTIFY:
                Events::eventEnter(ev);
                Debug::log(LOG, "Event dispatched ENTER");
                break;
            case XCB_DESTROY_NOTIFY:
                Events::eventDestroy(ev);
                Debug::log(LOG, "Event dispatched DESTROY");
                break;
            case XCB_MAP_REQUEST:
                Events::eventMapWindow(ev);
                Debug::log(LOG, "Event dispatched MAP");
                break;

            default:
                Debug::log(WARN, "Unknown event: " + std::to_string(ev->response_type & ~0x80));
                break;
        }

        free(ev);
    }

    // refresh and apply the parameters of all dirty windows.
    WindowManager::refreshDirtyWindows();

    xcb_flush(WindowManager::DisplayConnection);

    return true;
}

void WindowManager::refreshDirtyWindows() {
    for(auto& window : windows) {
        if (window.getDirty()) {
            Values[0] = (int)window.getSize().x;
            Values[1] = (int)window.getSize().y;
            xcb_configure_window(WindowManager::DisplayConnection, window.getDrawable(), XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, Values);

            Values[0] = (int)window.getPosition().x;
            Values[1] = (int)window.getPosition().y;
            xcb_configure_window(WindowManager::DisplayConnection, window.getDrawable(), XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, Values);

            window.setDirty(false);

            Debug::log(LOG, "Refreshed dirty window.");
        }
    }
}

void WindowManager::setFocusedWindow(xcb_drawable_t window) {
    if (window && window != WindowManager::Screen->root) {
        xcb_set_input_focus(WindowManager::DisplayConnection, XCB_INPUT_FOCUS_POINTER_ROOT, window, XCB_CURRENT_TIME);
        WindowManager::LastWindow = window;
    }
}

CWindow* WindowManager::getWindowFromDrawable(xcb_drawable_t window) {
    for(auto& w : WindowManager::windows) {
        if (w.getDrawable() == window) {
            return &w;
        }

        Debug::log(LOG, "Not " + std::to_string(w.getDrawable()));
    }
    return nullptr;
}

void WindowManager::addWindowToVectorSafe(CWindow window) {
    for (auto& w : WindowManager::windows) {
        if (w.getDrawable() == window.getDrawable())
            return; // Do not add if already present.
    }
    WindowManager::windows.push_back(window);
}

void WindowManager::removeWindowFromVectorSafe(xcb_drawable_t window) {

    if (!window)
        return;

    std::vector<CWindow> temp = WindowManager::windows;

    WindowManager::windows.clear();
    
    for(auto p : temp) {
        if (p.getDrawable() != window) {
            WindowManager::windows.push_back(p);
            continue;
        }
        Debug::log(LOG, "Is, removing: " + std::to_string(p.getDrawable()));
    }
}

void calculateNewTileSetOldTile(CWindow* pWindow) {
    const auto PLASTWINDOW = WindowManager::getWindowFromDrawable(WindowManager::LastWindow);
    if (PLASTWINDOW) {
        const auto PLASTSIZE = PLASTWINDOW->getSize();
        const auto PLASTPOS = PLASTWINDOW->getPosition();

        if (PLASTSIZE.x > PLASTSIZE.y) {
            PLASTWINDOW->setSize(Vector2D(PLASTSIZE.x / 2.f, PLASTSIZE.y));
            pWindow->setSize(Vector2D(PLASTSIZE.x / 2.f, PLASTSIZE.y));
            pWindow->setPosition(Vector2D(PLASTPOS.x + PLASTSIZE.x / 2.f, PLASTPOS.y));
        } else {
            PLASTWINDOW->setSize(Vector2D(PLASTSIZE.x, PLASTSIZE.y / 2.f));
            pWindow->setSize(Vector2D(PLASTSIZE.x, PLASTSIZE.y / 2.f));
            pWindow->setPosition(Vector2D(PLASTPOS.x, PLASTPOS.y + PLASTSIZE.y / 2.f));
        }

        PLASTWINDOW->setDirty(true);
    } else {
        // Open a fullscreen window
        pWindow->setSize(Vector2D(WindowManager::Screen->width_in_pixels, WindowManager::Screen->height_in_pixels));
        pWindow->setPosition(Vector2D(0, 0));
    }
}

void WindowManager::calculateNewWindowParams(CWindow* pWindow) {
    // And set old one's if needed.
    if (!pWindow)
        return;

    if (!pWindow->getIsFloating()) {
        calculateNewTileSetOldTile(pWindow);
    }

    pWindow->setDirty(true);
}

bool isNeighbor(CWindow* a, CWindow* b) {
    const auto POSA = a->getPosition();
    const auto POSB = b->getPosition();
    const auto SIZEA = a->getSize();
    const auto SIZEB = a->getSize();

    if (POSA.x != 0) {
        if (STICKS(POSA.x, (POSB.x + SIZEB.x))) {
            return true;
        }
    }
    if (POSA.y != 0) {
        if (STICKS(POSA.y, (POSB.y + SIZEB.y))) {
            return true;
        }
    }

    if (POSB.x != 0) {
        if (STICKS(POSB.x, (POSA.x + SIZEA.x))) {
            return true;
        }
    }
    if (POSB.y != 0) {
        if (STICKS(POSB.y, (POSA.y + SIZEA.y))) {
            return true;
        }
    }

    return false;
}

void eatWindow(CWindow* a, CWindow* toEat) {

    // Pos is min of both.
    a->setPosition(Vector2D(std::min(a->getPosition().x, toEat->getPosition().x), std::min(a->getPosition().y, toEat->getPosition().y)));

    // Size is pos + size max - pos
    const auto OPPCORNERA = a->getPosition() + a->getSize();
    const auto OPPCORNERB = toEat->getPosition() + toEat->getSize();

    a->setSize(Vector2D(std::max(OPPCORNERA.x, OPPCORNERB.x), std::max(OPPCORNERA.y, OPPCORNERB.y)) - a->getPosition());
}

void WindowManager::fixWindowOnClose(CWindow* pClosedWindow) {
    if (!pClosedWindow)
        return;

    // get the first neighboring window
    CWindow* neighbor = nullptr;
    for(auto& w : WindowManager::windows) {
        if (w.getDrawable() == pClosedWindow->getDrawable())
            continue;

        if (isNeighbor(&w, pClosedWindow)) {
            neighbor = &w;
            break;
        }
    }

    if (!neighbor)
        return; // No neighbor. Don't update, easy.


    // update neighbor to "eat" closed.
    eatWindow(neighbor, pClosedWindow);

    neighbor->setDirty(true);
}
// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_IME_MENU_IME_LIST_VIEW_H_
#define ASH_SYSTEM_IME_MENU_IME_LIST_VIEW_H_

#include <vector>

#include "ash/ash_export.h"
#include "ash/system/tray/tray_details_view.h"
#include "ui/views/controls/button/button.h"

namespace ash {

namespace mojom {
class ImeInfo;
class ImeMenuItem;
}

class KeyboardStatusRow;

// Base class used to represent a selecatable list of available IMEs.
// Optionally shows a toggle which is used to enable or disable the invocation
// of the virtual keyboard.
class ImeListView : public TrayDetailsView {
 public:
  enum SingleImeBehavior {
    // Shows the IME menu if there's only one IME in system.
    SHOW_SINGLE_IME,
    // Hides the IME menu if there's only one IME in system.
    HIDE_SINGLE_IME
  };

  explicit ImeListView(SystemTrayItem* owner);

  ~ImeListView() override;

  // Initializes the contents of a newly-instantiated ImeListView.
  void Init(bool show_keyboard_toggle, SingleImeBehavior single_ime_behavior);

  // Updates the view.
  virtual void Update(const std::vector<mojom::ImeInfo>& list,
                      const std::vector<mojom::ImeMenuItem>& property_items,
                      bool show_keyboard_toggle,
                      SingleImeBehavior single_ime_behavior);

  // Removes (and destroys) all child views.
  virtual void ResetImeListView();

  // Closes the view.
  void CloseImeListView();

  // Scrolls contents such that |item_view| is visible.
  void ScrollItemToVisible(views::View* item_view);

  void set_last_item_selected_with_keyboard(
      bool last_item_selected_with_keyboard) {
    last_item_selected_with_keyboard_ = last_item_selected_with_keyboard;
  }

  void set_should_focus_ime_after_selection_with_keyboard(
      const bool focus_current_ime) {
    should_focus_ime_after_selection_with_keyboard_ = focus_current_ime;
  }

  bool should_focus_ime_after_selection_with_keyboard() const {
    return should_focus_ime_after_selection_with_keyboard_;
  }

  // TrayDetailsView:
  void HandleViewClicked(views::View* view) override;
  void HandleButtonPressed(views::Button* sender,
                           const ui::Event& event) override;

  // views::View:
  void VisibilityChanged(View* starting_from, bool is_visible) override;

 private:
  friend class ImeListViewTestApi;

  // Appends the IMEs and properties to the IME menu's scrollable area.
  void AppendImeListAndProperties(
      const std::vector<mojom::ImeInfo>& list,
      const std::vector<mojom::ImeMenuItem>& property_items);

  // Initializes |keyboard_status_row_| and adds it above the scrollable list.
  void PrependKeyboardStatusRow();

  // Requests focus on the current IME if it was selected with keyboard so that
  // accessible text will alert the user of the IME change.
  void FocusCurrentImeIfNeeded();

  std::map<views::View*, std::string> ime_map_;
  std::map<views::View*, std::string> property_map_;
  KeyboardStatusRow* keyboard_status_row_;

  // The id of the last item selected with keyboard. It will be empty if the
  // item is not selected with keyboard.
  std::string last_selected_item_id_;

  // True if the last item is selected with keyboard.
  bool last_item_selected_with_keyboard_;

  // True if focus should be requested after switching IMEs with keyboard in
  // order to trigger spoken feedback with ChromeVox enabled.
  bool should_focus_ime_after_selection_with_keyboard_;

  // The item view of the current selected IME.
  views::View* current_ime_view_;

  DISALLOW_COPY_AND_ASSIGN(ImeListView);
};

class ASH_EXPORT ImeListViewTestApi {
 public:
  explicit ImeListViewTestApi(ImeListView* ime_list_view);
  virtual ~ImeListViewTestApi();

  views::View* GetToggleView() const;

  const std::map<views::View*, std::string>& ime_map() const {
    return ime_list_view_->ime_map_;
  }

 private:
  ImeListView* ime_list_view_;

  DISALLOW_COPY_AND_ASSIGN(ImeListViewTestApi);
};

}  // namespace ash

#endif  // ASH_SYSTEM_IME_MENU_IME_LIST_VIEW_H_

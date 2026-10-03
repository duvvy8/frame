"""Apply Frame's first native Chromium Views rail to the pinned checkout."""

from pathlib import Path

SRC = Path(__file__).resolve().parents[1] / "chromium" / "src"


def change(name: str, before: str, after: str) -> None:
    path = SRC / name
    source = path.read_text(encoding="utf-8")
    if after in source:
        print(f"already applied: {name}")
        return
    if source.count(before) != 1:
        raise RuntimeError(f"expected one source match in {name}: {before[:70]!r}")
    path.write_text(source.replace(before, after), encoding="utf-8", newline="")
    print(f"updated: {name}")


def main() -> None:
    cc = "chrome/browser/ui/views/frame/browser_view.cc"
    header = "chrome/browser/ui/views/frame/browser_view.h"
    layout_header = "chrome/browser/ui/views/frame/layout/browser_view_layout.h"
    layout_cc = "chrome/browser/ui/views/frame/layout/browser_view_tabbed_layout_impl.cc"

    change(cc,
           '#include "chrome/browser/ui/browser_commands.h"\n',
           '#include "chrome/browser/ui/browser_commands.h"\n'
           '#include "chrome/browser/ui/navigator/browser_navigator.h"\n'
           '#include "chrome/browser/ui/navigator/browser_navigator_params.h"\n'
           '#include "chrome/browser/ui/tabs/tab_enums.h"\n')
    change(cc,
           '#include <utility>\n',
           '#include <utility>\n#include <vector>\n')
    change(cc,
           '#include "base/command_line.h"\n',
           '#include "base/command_line.h"\n'
           '#include "base/strings/utf_string_conversions.h"\n')
    change(cc,
           '#include "ui/views/view.h"\n',
           '#include "ui/views/view.h"\n'
           '#include "ui/views/background.h"\n'
           '#include "ui/views/controls/button/label_button.h"\n'
           '#include "ui/views/controls/label.h"\n'
           '#include "ui/views/layout/box_layout.h"\n')
    rail_class = '''// Frame's permanent rail is native browser chrome. Page content, extension
// pages, and the Chromium tab model remain separate from these controls.
class FrameRailView : public views::View {
 public:
  explicit FrameRailView(BrowserWindowInterface* browser) : browser_(browser) {
    SetBackground(views::CreateSolidBackground(SkColorSetRGB(24, 12, 45)));
    SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(8, 12, 12, 12), 8));
    SetPreferredSize(gfx::Size(176, 0));

    auto* heading = AddChildView(std::make_unique<views::View>());
    heading->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal));
    toggle_ = heading->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&FrameRailView::Toggle, base::Unretained(this)),
        u"◧"));
    toggle_->SetPreferredSize(gfx::Size(24, 28));
    toggle_->SetEnabledTextColors(SK_ColorWHITE);
    auto* title = heading->AddChildView(std::make_unique<views::Label>(u"FRAME"));
    title->SetPreferredSize(gfx::Size(112, 28));
    title->SetEnabledColor(SK_ColorWHITE);
    expanded_children_.push_back(title);

    auto* address = AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&FrameRailView::FocusAddress,
                            base::Unretained(this)),
        u"Search or enter URL"));
    address->SetPreferredSize(gfx::Size(152, 34));
    address->SetBackground(views::CreateRoundedRectBackground(
        SkColorSetRGB(48, 34, 88), 8));
    address->SetEnabledTextColors(SK_ColorWHITE);
    expanded_children_.push_back(address);

    AddFavoriteRow(u"G", GURL("https://www.google.com/"),
                   u"GH", GURL("https://github.com/"));
    AddFavoriteRow(u"W", GURL("https://www.wikipedia.org/"),
                   u"YT", GURL("https://www.youtube.com/"));

    AddNavigationRow(u"+ New tab", &FrameRailView::OpenNewTab,
                     u"← Back", &FrameRailView::GoBack);
    AddNavigationRow(u"→ Forward", &FrameRailView::GoForward,
                     u"↻ Refresh", &FrameRailView::Refresh);
  }

 private:
  void Toggle() {
    collapsed_ = !collapsed_;
    for (auto& child : expanded_children_) {
      child->SetVisible(!collapsed_);
    }
    static_cast<views::BoxLayout*>(GetLayoutManager())
        ->set_inside_border_insets(
            collapsed_ ? gfx::Insets::TLBR(8, 4, 4, 4)
                       : gfx::Insets::TLBR(8, 12, 12, 12));
    SetPreferredSize(gfx::Size(collapsed_ ? 36 : 176, 0));
    parent()->InvalidateLayout();
  }

  void AddFavoriteRow(const std::u16string& first, const GURL& first_url,
                      const std::u16string& second, const GURL& second_url) {
    auto* row = AddChildView(std::make_unique<views::View>());
    row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
    AddFavorite(row, first, first_url);
    AddFavorite(row, second, second_url);
    expanded_children_.push_back(row);
  }

  void AddFavorite(views::View* row, const std::u16string& text,
                   const GURL& url) {
    auto* button = row->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(&FrameRailView::OpenFavorite,
                            base::Unretained(this), url), text));
    button->SetPreferredSize(gfx::Size(72, 48));
    button->SetBackground(views::CreateRoundedRectBackground(
        SkColorSetRGB(61, 49, 149), 9));
    button->SetEnabledTextColors(SK_ColorWHITE);
    button->SetTooltipText(base::UTF8ToUTF16(url.host()));
  }

  void AddNavigationRow(const std::u16string& first,
                        void (FrameRailView::*first_method)(),
                        const std::u16string& second,
                        void (FrameRailView::*second_method)()) {
    auto* row = AddChildView(std::make_unique<views::View>());
    row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
    AddRailButton(row, first, first_method);
    AddRailButton(row, second, second_method);
    expanded_children_.push_back(row);
  }

  void AddRailButton(views::View* row, const std::u16string& text,
                     void (FrameRailView::*method)()) {
    auto* button = row->AddChildView(std::make_unique<views::LabelButton>(
        base::BindRepeating(method, base::Unretained(this)), text));
    button->SetPreferredSize(gfx::Size(72, 36));
    button->SetBackground(views::CreateRoundedRectBackground(
        SkColorSetRGB(48, 41, 104), 9));
    button->SetEnabledTextColors(SK_ColorWHITE);
  }

  void FocusAddress() {
    chrome::FocusLocationBar(browser_);
  }
  void OpenFavorite(const GURL& url) {
    NavigateParams params(browser_, url, ui::PAGE_TRANSITION_AUTO_BOOKMARK);
    params.disposition = WindowOpenDisposition::CURRENT_TAB;
    ::Navigate(&params);
  }

  void OpenNewTab() {
    chrome::NewTab(browser_, NewTabTypes::kNewTabButton);
  }
  void GoBack() {
    if (chrome::CanGoBack(browser_)) {
      chrome::GoBack(browser_, WindowOpenDisposition::CURRENT_TAB);
    }
  }
  void GoForward() {
    if (chrome::CanGoForward(browser_)) {
      chrome::GoForward(browser_, WindowOpenDisposition::CURRENT_TAB);
    }
  }
  void Refresh() {
    chrome::Reload(browser_, WindowOpenDisposition::CURRENT_TAB);
  }

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<views::LabelButton> toggle_ = nullptr;
  std::vector<raw_ptr<views::View>> expanded_children_;
  bool collapsed_ = false;
};

'''
    rail_path = SRC / cc
    source = rail_path.read_text(encoding="utf-8")
    existing_marker = "// Frame's permanent rail is native browser chrome."
    ctor_marker = "BrowserView::BrowserView(BrowserWindowInterface* browser)\n"
    if existing_marker in source and rail_class not in source:
        start = source.index(existing_marker)
        end = source.index(ctor_marker, start)
        rail_path.write_text(source[:start] + rail_class + source[end:],
                             encoding="utf-8", newline="")
        print(f"updated rail: {cc}")
    change(cc, 'BrowserView::BrowserView(BrowserWindowInterface* browser)\n',
           rail_class + 'BrowserView::BrowserView(BrowserWindowInterface* browser)\n')
    change(cc,
           '  main_background_region_ =\n      AddChildView(std::make_unique<MainBackgroundRegionView>(*this));\n',
           '  if (GetIsNormalType()) {\n'
           '    frame_rail_ = AddChildView(std::unique_ptr<views::View>(\n'
           '        std::make_unique<FrameRailView>(browser_.get())));\n'
           '  }\n\n'
           '  main_background_region_ =\n      AddChildView(std::make_unique<MainBackgroundRegionView>(*this));\n')
    change(cc,
           '  layout_views.main_background_region = main_background_region_;\n',
           '  layout_views.main_background_region = main_background_region_;\n'
           '  layout_views.frame_rail = frame_rail_;\n')
    change(cc,
           '  main_background_region_ = nullptr;\n',
           '  main_background_region_ = nullptr;\n'
           '  frame_rail_ = nullptr;\n')
    change(header,
           '  raw_ptr<views::View> main_background_region_ = nullptr;\n',
           '  raw_ptr<views::View> main_background_region_ = nullptr;\n'
           '  raw_ptr<views::View> frame_rail_ = nullptr;\n')
    change(layout_header,
           '  raw_ptr<views::View> main_background_region = nullptr;\n',
           '  raw_ptr<views::View> main_background_region = nullptr;\n'
           '  raw_ptr<views::View> frame_rail = nullptr;\n')
    old_layout = (
        '    constexpr int kFrameRailWidth = 176;\n'
        '    layout.AddChild(views().frame_rail,\n'
        '                    gfx::Rect(browser_params.visual_client_area.x(),\n'
        '                              browser_params.visual_client_area.y(),\n'
        '                              kFrameRailWidth,\n'
        '                              browser_params.visual_client_area.height()));\n'
        '    params.InsetHorizontal(kFrameRailWidth, /*leading=*/true);\n')
    new_layout = (
        '    const int rail_width = views().frame_rail->GetPreferredSize().width();\n'
        '    layout.AddChild(views().frame_rail,\n'
        '                    gfx::Rect(browser_params.visual_client_area.x(),\n'
        '                              browser_params.visual_client_area.y(),\n'
        '                              rail_width,\n'
        '                              browser_params.visual_client_area.height()));\n'
        '    params.InsetHorizontal(rail_width, /*leading=*/true);\n')
    layout_path = SRC / layout_cc
    layout_source = layout_path.read_text(encoding="utf-8")
    if old_layout in layout_source:
        layout_path.write_text(layout_source.replace(old_layout, new_layout),
                               encoding="utf-8", newline="")
        print(f"updated rail width: {layout_cc}")
    change(layout_cc,
           '  BrowserLayoutParams params = layout_data_->revised_params;\n'
           '  bool needs_exclusion = true;\n',
           '  BrowserLayoutParams params = layout_data_->revised_params;\n'
           '  if (IsParentedTo(views().frame_rail, views().browser_view)) {\n'
           '    const int rail_width = views().frame_rail->GetPreferredSize().width();\n'
           '    layout.AddChild(views().frame_rail,\n'
           '                    gfx::Rect(browser_params.visual_client_area.x(),\n'
           '                              browser_params.visual_client_area.y(),\n'
           '                              rail_width,\n'
           '                              browser_params.visual_client_area.height()));\n'
           '    params.InsetHorizontal(rail_width, /*leading=*/true);\n'
           '  }\n'
           '  bool needs_exclusion = true;\n')


if __name__ == "__main__":
    main()

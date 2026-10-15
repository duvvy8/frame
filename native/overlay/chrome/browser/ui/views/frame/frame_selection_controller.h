// Frame native selection popup. GPL-3.0; see Frame repository LICENSE.
#ifndef CHROME_BROWSER_UI_VIEWS_FRAME_FRAME_SELECTION_CONTROLLER_H_
#define CHROME_BROWSER_UI_VIEWS_FRAME_FRAME_SELECTION_CONTROLLER_H_

#include <cstdint>
#include <string>
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/weak_document_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/widget/widget_observer.h"

class BrowserView;
namespace views { class Widget; }

// One transient selection surface per browser window. Observes only the native
// active contents, never subscribes to hidden pages or wakes discarded tabs.
class FrameSelectionController : public content::WebContentsObserver,
                                 public views::WidgetObserver {
 public:
  explicit FrameSelectionController(BrowserView* owner);
  ~FrameSelectionController() override;
  void SetContents(content::WebContents* contents);
  void OnTextSelectionChanged(content::RenderFrameHost* frame,
                              std::u16string_view text) override;
  void PrimaryPageChanged(content::Page& page) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  void DidGetUserInteraction(const blink::WebInputEvent& event) override;
  void DidChangeVerticalScrollDirection(viz::VerticalScrollDirection direction) override;
  void WebContentsDestroyed() override;
  void OnWidgetBoundsChanged(views::Widget* widget, const gfx::Rect& bounds) override;
  void OnWidgetDestroying(views::Widget* widget) override;

 private:
  void Dismiss();
  void Show(uint64_t revision);
  void Copy(uint64_t revision);
  bool CanUseSelection() const;
  base::WeakPtr<BrowserView> owner_;
  raw_ptr<views::Widget> owner_widget_ = nullptr;
  raw_ptr<views::Widget> popup_ = nullptr;
  content::WeakDocumentPtr document_;
  std::u16string selected_text_;
  uint64_t revision_ = 0;
  PrefChangeRegistrar prefs_;
  base::WeakPtrFactory<FrameSelectionController> weak_factory_{this};
};
#endif

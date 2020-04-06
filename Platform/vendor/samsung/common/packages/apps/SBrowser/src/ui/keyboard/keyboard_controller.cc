// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/keyboard/keyboard_controller.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "ui/aura/layout_manager.h"
#include "ui/aura/window.h"
#include "ui/aura/window_delegate.h"
#include "ui/base/cursor/cursor.h"
#include "ui/base/hit_test.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/compositor/layer_animation_observer.h"
#include "ui/compositor/scoped_layer_animation_settings.h"
#include "ui/gfx/path.h"
#include "ui/gfx/rect.h"
#include "ui/gfx/skia_util.h"
#include "ui/keyboard/keyboard_controller_observer.h"
#include "ui/keyboard/keyboard_controller_proxy.h"
#include "ui/keyboard/keyboard_switches.h"
#include "ui/keyboard/keyboard_util.h"
#include "ui/wm/public/masked_window_targeter.h"

namespace {

const int kHideKeyboardDelayMs = 100;

// The virtual keyboard show/hide animation duration.
const int kAnimationDurationMs = 200;

// The opacity of virtual keyboard container when show animation starts or
// hide animation finishes.
const float kAnimationStartOrAfterHideOpacity = 0.2f;

// The ratio between the height of the keyboard and the screen when using the
// usability keyboard.
const float kUsabilityKeyboardHeightRatio = 1.0f;

// The default ratio between the height of the keyboard and the screen.
const float kDefaultKeyboardHeightRatio = 0.3f;

// The ratio between the height of the keyboard and the screen when using the
// accessibility keyboard.
const float kAccessibilityKeyboardHeightRatio = 0.3f;

float GetKeyboardHeightRatio(){
  if (keyboard::IsKeyboardUsabilityExperimentEnabled()) {
    return kUsabilityKeyboardHeightRatio;
  } else if (keyboard::GetAccessibilityKeyboardEnabled()) {
    return kAccessibilityKeyboardHeightRatio;
  }
  return kDefaultKeyboardHeightRatio;
}
gfx::Rect KeyboardBoundsFromWindowBounds(const gfx::Rect& window_bounds) {
  const float kKeyboardHeightRatio = GetKeyboardHeightRatio();
  return gfx::Rect(
      window_bounds.x(),
      window_bounds.y() + window_bounds.height() * (1 - kKeyboardHeightRatio),
      window_bounds.width(),
      window_bounds.height() * kKeyboardHeightRatio);
}

// Event targeter for the keyboard container.
class KeyboardContainerTargeter : public wm::MaskedWindowTargeter {
 public:
  KeyboardContainerTargeter(aura::Window* container,
                            keyboard::KeyboardControllerProxy* proxy)
      : wm::MaskedWindowTargeter(container),
        proxy_(proxy) {
  }

  virtual ~KeyboardContainerTargeter() {}

 private:
  // wm::MaskedWindowTargeter:
  virtual bool GetHitTestMask(aura::Window* window,
                              gfx::Path* mask) const OVERRIDE {
    gfx::Rect keyboard_bounds = proxy_ ? proxy_->GetKeyboardWindow()->bounds() :
        KeyboardBoundsFromWindowBounds(window->bounds());
    mask->addRect(RectToSkRect(keyboard_bounds));
    return true;
  }

  keyboard::KeyboardControllerProxy* proxy_;

  DISALLOW_COPY_AND_ASSIGN(KeyboardContainerTargeter);
};

// The KeyboardWindowDelegate makes sure the keyboard-window does not get focus.
// This is necessary to make sure that the synthetic key-events reach the target
// window.
// The delegate deletes itself when the window is destroyed.
class KeyboardWindowDelegate : public aura::WindowDelegate {
 public:
  explicit KeyboardWindowDelegate(keyboard::KeyboardControllerProxy* proxy)
      : proxy_(proxy) {}
  virtual ~KeyboardWindowDelegate() {}

 private:
  // Overridden from aura::WindowDelegate:
  virtual gfx::Size GetMinimumSize() const OVERRIDE { return gfx::Size(); }
  virtual gfx::Size GetMaximumSize() const OVERRIDE { return gfx::Size(); }
  virtual void OnBoundsChanged(const gfx::Rect& old_bounds,
                               const gfx::Rect& new_bounds) OVERRIDE {
    bounds_ = new_bounds;
  }
  virtual gfx::NativeCursor GetCursor(const gfx::Point& point) OVERRIDE {
    return gfx::kNullCursor;
  }
  virtual int GetNonClientComponent(const gfx::Point& point) const OVERRIDE {
    return HTNOWHERE;
  }
  virtual bool ShouldDescendIntoChildForEventHandling(
      aura::Window* child,
      const gfx::Point& location) OVERRIDE {
    return true;
  }
  virtual bool CanFocus() OVERRIDE { return false; }
  virtual void OnCaptureLost() OVERRIDE {}
  virtual void OnPaint(gfx::Canvas* canvas) OVERRIDE {}
  virtual void OnDeviceScaleFactorChanged(float device_scale_factor) OVERRIDE {}
  virtual void OnWindowDestroying() OVERRIDE {}
  virtual void OnWindowDestroyed() OVERRIDE { delete this; }
  virtual void OnWindowTargetVisibilityChanged(bool visible) OVERRIDE {}
  virtual bool HasHitTestMask() const OVERRIDE { return true; }
  virtual void GetHitTestMask(gfx::Path* mask) const OVERRIDE {
    gfx::Rect keyboard_bounds = proxy_ ? proxy_->GetKeyboardWindow()->bounds() :
        KeyboardBoundsFromWindowBounds(bounds_);
    mask->addRect(RectToSkRect(keyboard_bounds));
  }
  virtual void DidRecreateLayer(ui::Layer* old_layer,
                                ui::Layer* new_layer) OVERRIDE {}

  gfx::Rect bounds_;
  keyboard::KeyboardControllerProxy* proxy_;

  DISALLOW_COPY_AND_ASSIGN(KeyboardWindowDelegate);
};

}  // namespace

namespace keyboard {

// Observer for both keyboard show and hide animations. It should be owned by
// KeyboardController.
class CallbackAnimationObserver : public ui::LayerAnimationObserver {
 public:
  CallbackAnimationObserver(ui::LayerAnimator* animator,
                            base::Callback<void(void)> callback);
  virtual ~CallbackAnimationObserver();

 private:
  // Overridden from ui::LayerAnimationObserver:
  virtual void OnLayerAnimationEnded(ui::LayerAnimationSequence* seq) OVERRIDE;
  virtual void OnLayerAnimationAborted(
      ui::LayerAnimationSequence* seq) OVERRIDE;
  virtual void OnLayerAnimationScheduled(
      ui::LayerAnimationSequence* seq) OVERRIDE {}

  ui::LayerAnimator* animator_;
  base::Callback<void(void)> callback_;

  DISALLOW_COPY_AND_ASSIGN(CallbackAnimationObserver);
};

CallbackAnimationObserver::CallbackAnimationObserver(
    ui::LayerAnimator* animator, base::Callback<void(void)> callback)
    : animator_(animator), callback_(callback) {
}

CallbackAnimationObserver::~CallbackAnimationObserver() {
  animator_->RemoveObserver(this);
}

void CallbackAnimationObserver::OnLayerAnimationEnded(
    ui::LayerAnimationSequence* seq) {
  if (animator_->is_animating())
    return;
  animator_->RemoveObserver(this);
  callback_.Run();
}

void CallbackAnimationObserver::OnLayerAnimationAborted(
    ui::LayerAnimationSequence* seq) {
  animator_->RemoveObserver(this);
}

// LayoutManager for the virtual keyboard container.  Manages a single window
// (the virtual keyboard) and keeps it positioned at the bottom of the
// owner window.
class KeyboardLayoutManager : public aura::LayoutManager {
 public:
  explicit KeyboardLayoutManager(KeyboardController* controller)
      : controller_(controller), keyboard_(NULL) {
  }

  // Overridden from aura::LayoutManager
  virtual void OnWindowResized() OVERRIDE {
    if (keyboard_ && !controller_->proxy()->resizing_from_contents())
      ResizeKeyboardToDefault(keyboard_);
  }
  virtual void OnWindowAddedToLayout(aura::Window* child) OVERRIDE {
    DCHECK(!keyboard_);
    keyboard_ = child;
    ResizeKeyboardToDefault(keyboard_);
  }
  virtual void OnWillRemoveWindowFromLayout(aura::Window* child) OVERRIDE {}
  virtual void OnWindowRemovedFromLayout(aura::Window* child) OVERRIDE {}
  virtual void OnChildWindowVisibilityChanged(aura::Window* child,
                                              bool visible) OVERRIDE {}
  virtual void SetChildBounds(aura::Window* child,
                              const gfx::Rect& requested_bounds) OVERRIDE {
    // SetChildBounds can be invoked by resizing from the container or by
    // resizing from the contents (through window.resizeTo call in JS).
    // The flag resizing_from_contents() is used to determine the keyboard is
    // resizing from which.
    if (controller_->proxy()->resizing_from_contents()) {
      controller_->NotifyKeyboardBoundsChanging(requested_bounds);
      SetChildBoundsDirect(child, requested_bounds);
    }
  }

 private:
  void ResizeKeyboardToDefault(aura::Window* child) {
    gfx::Rect keyboard_bounds = KeyboardBoundsFromWindowBounds(
        controller_->GetContainerWindow()->bounds());
    SetChildBoundsDirect(child, keyboard_bounds);
  }

  KeyboardController* controller_;
  aura::Window* keyboard_;

  DISALLOW_COPY_AND_ASSIGN(KeyboardLayoutManager);
};

KeyboardController::KeyboardController(KeyboardControllerProxy* proxy)
    : proxy_(proxy),
      input_method_(NULL),
      keyboard_visible_(false),
      lock_keyboard_(false),
      weak_factory_(this) {
  CHECK(proxy);
  input_method_ = proxy_->GetInputMethod();
  input_method_->AddObserver(this);
}

KeyboardController::~KeyboardController() {
  if (container_)
    container_->RemoveObserver(this);
  if (input_method_)
    input_method_->RemoveObserver(this);
}

aura::Window* KeyboardController::GetContainerWindow() {
  if (!container_.get()) {
    container_.reset(new aura::Window(
        new KeyboardWindowDelegate(proxy_.get())));
    container_->SetEventTargeter(scoped_ptr<ui::EventTargeter>(
        new KeyboardContainerTargeter(container_.get(), proxy_.get())));
    container_->SetName("KeyboardContainer");
    container_->set_owned_by_parent(false);
    container_->Init(aura::WINDOW_LAYER_NOT_DRAWN);
    container_->AddObserver(this);
    container_->SetLayoutManager(new KeyboardLayoutManager(this));
  }
  return container_.get();
}

void KeyboardController::NotifyKeyboardBoundsChanging(
    const gfx::Rect& new_bounds) {
  if (proxy_->HasKeyboardWindow() && proxy_->GetKeyboardWindow()->IsVisible()) {
    FOR_EACH_OBSERVER(KeyboardControllerObserver,
                      observer_list_,
                      OnKeyboardBoundsChanging(new_bounds));
  }
}

void KeyboardController::HideKeyboard(HideReason reason) {
  keyboard_visible_ = false;

  keyboard::LogKeyboardControlEvent(
      reason == HIDE_REASON_AUTOMATIC ?
          keyboard::KEYBOARD_CONTROL_HIDE_AUTO :
          keyboard::KEYBOARD_CONTROL_HIDE_USER);

  NotifyKeyboardBoundsChanging(gfx::Rect());

  ui::LayerAnimator* container_animator = container_->layer()->GetAnimator();
  animation_observer_.reset(new CallbackAnimationObserver(
      container_animator,
      base::Bind(&KeyboardController::HideAnimationFinished,
                 base::Unretained(this))));
  container_animator->AddObserver(animation_observer_.get());

  ui::ScopedLayerAnimationSettings settings(container_animator);
  settings.SetTweenType(gfx::Tween::EASE_OUT);
  settings.SetTransitionDuration(
      base::TimeDelta::FromMilliseconds(kAnimationDurationMs));
  gfx::Transform transform;
  transform.Translate(0, proxy_->GetKeyboardWindow()->bounds().height());
  container_->SetTransform(transform);
  container_->layer()->SetOpacity(kAnimationStartOrAfterHideOpacity);
}

void KeyboardController::AddObserver(KeyboardControllerObserver* observer) {
  observer_list_.AddObserver(observer);
}

void KeyboardController::RemoveObserver(KeyboardControllerObserver* observer) {
  observer_list_.RemoveObserver(observer);
}

void KeyboardController::OnWindowHierarchyChanged(
    const HierarchyChangeParams& params) {
  if (params.new_parent && params.target == container_.get())
    OnTextInputStateChanged(proxy_->GetInputMethod()->GetTextInputClient());
}

void KeyboardController::SetOverrideContentUrl(const GURL& url) {
  proxy_->SetOverrideContentUrl(url);
}

void KeyboardController::OnTextInputStateChanged(
    const ui::TextInputClient* client) {
  if (!container_.get())
    return;

  if (IsKeyboardUsabilityExperimentEnabled()) {
    OnShowImeIfNeeded();
    return;
  }

  ui::TextInputType type =
      client ? client->GetTextInputType() : ui::TEXT_INPUT_TYPE_NONE;
  if (type == ui::TEXT_INPUT_TYPE_NONE && !lock_keyboard_) {
    if (keyboard_visible_) {
      // Set the visibility state here so that any queries for visibility
      // before the timer fires returns the correct future value.
      keyboard_visible_ = false;
      base::MessageLoop::current()->PostDelayedTask(
          FROM_HERE,
          base::Bind(&KeyboardController::HideKeyboard,
                     weak_factory_.GetWeakPtr(), HIDE_REASON_AUTOMATIC),
          base::TimeDelta::FromMilliseconds(kHideKeyboardDelayMs));
    }
  } else {
    // Abort a pending keyboard hide.
    if (WillHideKeyboard()) {
      weak_factory_.InvalidateWeakPtrs();
      keyboard_visible_ = true;
    }
    proxy_->SetUpdateInputType(type);
    // Do not explicitly show the Virtual keyboard unless it is in the process
    // of hiding. Instead, the virtual keyboard is shown in response to a user
    // gesture (mouse or touch) that is received while an element has input
    // focus. Showing the keyboard requires an explicit call to
    // OnShowImeIfNeeded.
  }
  // TODO(bryeung): whenever the TextInputClient changes we need to notify the
  // keyboard (with the TextInputType) so that it can reset it's state (e.g.
  // abandon compositions in progress)
}

void KeyboardController::OnInputMethodDestroyed(
    const ui::InputMethod* input_method) {
  DCHECK_EQ(input_method_, input_method);
  input_method_ = NULL;
}

void KeyboardController::OnShowImeIfNeeded() {
  if (!container_.get())
    return;

  if (container_->children().empty()) {
    keyboard::MarkKeyboardLoadStarted();
    aura::Window* keyboard = proxy_->GetKeyboardWindow();
    keyboard->Show();
    container_->AddChild(keyboard);
    keyboard->set_owned_by_parent(false);
  }
  if (keyboard_visible_)
    return;

  keyboard_visible_ = true;

  // If the controller is in the process of hiding the keyboard, do not log
  // the stat here since the keyboard will not actually be shown.
  if (!WillHideKeyboard())
    keyboard::LogKeyboardControlEvent(keyboard::KEYBOARD_CONTROL_SHOW);

  weak_factory_.InvalidateWeakPtrs();

  // If |container_| has hide animation, its visibility is set to false when
  // hide animation finished. So even if the container is visible at this
  // point, it may in the process of hiding. We still need to show keyboard
  // container in this case.
  if (container_->IsVisible() &&
      !container_->layer()->GetAnimator()->is_animating())
    return;

  ShowKeyboard();
}

void KeyboardController::ShowKeyboard() {
  ui::LayerAnimator* container_animator = container_->layer()->GetAnimator();

  // If the container is not animating, makes sure the position and opacity
  // are at begin states for animation.
  if (!container_animator->is_animating()) {
    gfx::Transform transform;
    transform.Translate(0, proxy_->GetKeyboardWindow()->bounds().height());
    container_->SetTransform(transform);
    container_->layer()->SetOpacity(kAnimationStartOrAfterHideOpacity);
  }

  container_animator->set_preemption_strategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  animation_observer_.reset(new CallbackAnimationObserver(
      container_animator,
      base::Bind(&KeyboardController::ShowAnimationFinished,
                 base::Unretained(this))));
  container_animator->AddObserver(animation_observer_.get());

  {
    // Scope the following animation settings as we don't want to animate
    // visibility change that triggered by a call to the base class function
    // ShowKeyboardContainer with these settings. The container should become
    // visible immediately.
    ui::ScopedLayerAnimationSettings settings(container_animator);
    settings.SetTweenType(gfx::Tween::EASE_IN);
    settings.SetTransitionDuration(
        base::TimeDelta::FromMilliseconds(kAnimationDurationMs));
    container_->SetTransform(gfx::Transform());
    container_->layer()->SetOpacity(1.0);
  }

  proxy_->ShowKeyboardContainer(container_.get());
}

bool KeyboardController::WillHideKeyboard() const {
  return weak_factory_.HasWeakPtrs();
}

void KeyboardController::ShowAnimationFinished() {
  // Notify observers after animation finished to prevent reveal desktop
  // background during animation.
  NotifyKeyboardBoundsChanging(proxy_->GetKeyboardWindow()->bounds());
  proxy_->EnsureCaretInWorkArea();
}

void KeyboardController::HideAnimationFinished() {
  proxy_->HideKeyboardContainer(container_.get());
}

}  // namespace keyboard
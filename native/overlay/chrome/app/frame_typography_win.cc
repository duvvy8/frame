// Copyright 2026 The Ember Authors
// Use of this source code is governed by a BSD-style license.
#include "chrome/app/frame_typography_win.h"

#include <windows.h>

#include <utility>
#include <vector>

#include "base/check.h"
#include "base/strings/string_util.h"
#include "base/win/current_module.h"
#include "skia/ext/font_utils.h"
#include "third_party/skia/include/core/SkData.h"
#include "third_party/skia/include/core/SkFontArguments.h"
#include "third_party/skia/include/core/SkFontMgr.h"
#include "third_party/skia/include/core/SkFontStyle.h"
#include "third_party/skia/include/core/SkStream.h"
#include "third_party/skia/include/core/SkString.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "third_party/skia/include/ports/SkTypeface_win.h"

namespace frame {
namespace {

// Geist is registered only in this browser process. Its variable weight axis
// provides every UI weight without depending on a system font installation.
// The resource memory and private GDI registration live until process exit.
class GeistStyleSet final : public SkFontStyleSet {
 public:
  GeistStyleSet(const sk_sp<SkFontMgr>& system, const wchar_t* resource_name) {
    HMODULE module = CURRENT_MODULE();
    HRSRC resource = FindResourceW(module, resource_name, RT_RCDATA);
    if (!resource)
      return;
    const DWORD size = SizeofResource(module, resource);
    void* bytes = LockResource(LoadResource(module, resource));
    if (!bytes || !size)
      return;
    DWORD count = 0;
    if (!AddFontMemResourceEx(bytes, size, nullptr, &count))
      return;
    auto variable_face =
        system->makeFromData(SkData::MakeWithoutCopy(bytes, size));
    if (!variable_face)
      return;
    for (int weight = 100; weight <= 900; weight += 100) {
      for (bool italic : {false, true}) {
        const SkFontArguments::VariationPosition::Coordinate coordinate = {
            SkFontArguments::VariationPosition::Coordinate::wght,
            static_cast<float>(weight)};
        SkFontArguments arguments;
        arguments.setVariationDesignPosition({&coordinate, 1});
        // This licensed face has a weight axis, but no italic axis. Preserve
        // native UI emphasis with Skia's supported synthetic oblique fallback.
        arguments.setSyntheticOblique(italic);
        auto face = variable_face->makeClone(arguments);
        if (face) {
          faces_.push_back(std::move(face));
          styles_.emplace_back(weight, SkFontStyle::kNormal_Width,
                              italic ? SkFontStyle::kItalic_Slant
                                     : SkFontStyle::kUpright_Slant);
        }
      }
    }
  }
  int count() override { return static_cast<int>(faces_.size()); }
  void getStyle(int index, SkFontStyle* style, SkString* name) override {
    *style = styles_[index];
    if (name) {
      name->reset();
    }
  }
  sk_sp<SkTypeface> createTypeface(int index) override { return faces_[index]; }
  sk_sp<SkTypeface> matchStyle(const SkFontStyle& style) override {
    return matchStyleCSS3(style);
  }
 private:
  std::vector<sk_sp<SkTypeface>> faces_;
  std::vector<SkFontStyle> styles_;
};

bool IsGeist(const char* name) {
  return name && base::EqualsCaseInsensitiveASCII(name, "Geist");
}

// This manager is installed ONLY in the browser. It adds two explicit families;
// the system manager continues to own every other family, fallback character,
// font stream and default. Renderer/GPU/utility processes never execute this.
class BrowserFontManager final : public SkFontMgr {
 public:
  BrowserFontManager()
      : system_(SkFontMgr_New_DirectWrite()),
        geist_(sk_make_sp<GeistStyleSet>(system_, L"FRAME_GEIST")),
        mono_(sk_make_sp<GeistStyleSet>(system_, L"FRAME_GEIST_MONO")) {}
 private:
  int onCountFamilies() const override {
    return system_->countFamilies() + 2;
  }
  void onGetFamilyName(int index, SkString* name) const override {
    if (index == system_->countFamilies()) {
      name->set("Geist");
    } else if (index == system_->countFamilies() + 1) {
      name->set("Geist Mono");
    } else {
      system_->getFamilyName(index, name);
    }
  }
  sk_sp<SkFontStyleSet> onCreateStyleSet(int index) const override {
    if (index == system_->countFamilies()) return geist_;
    if (index == system_->countFamilies() + 1) return mono_;
    return system_->createStyleSet(index);
  }
  sk_sp<SkFontStyleSet> onMatchFamily(const char name[]) const override {
    auto family = BundledFamily(name);
    return family ? family : system_->matchFamily(name);
  }
  sk_sp<SkTypeface> onMatchFamilyStyle(const char name[],
                                     const SkFontStyle& style) const override {
    auto family = BundledFamily(name);
    return family ? family->matchStyle(style) : system_->matchFamilyStyle(name, style);
  }
  sk_sp<SkTypeface> onMatchFamilyStyleCharacter(
      const char name[], const SkFontStyle& style, const char* languages[],
      int count, SkUnichar character) const override {
    if (auto family = BundledFamily(name)) {
      auto face = family->matchStyle(style);
      if (face && face->unicharToGlyph(character)) {
        return face;
      }
    }
    return system_->matchFamilyStyleCharacter(name, style, languages, count,
                                              character);
  }
  sk_sp<SkTypeface> onMakeFromData(sk_sp<SkData> data, int index) const override {
    return system_->makeFromData(std::move(data), index);
  }
  sk_sp<SkTypeface> onMakeFromStreamIndex(std::unique_ptr<SkStreamAsset> stream,
                                        int index) const override {
    return system_->makeFromStream(std::move(stream), index);
  }
  sk_sp<SkTypeface> onMakeFromStreamArgs(std::unique_ptr<SkStreamAsset> stream,
                                       const SkFontArguments& args) const override {
    return system_->makeFromStream(std::move(stream), args);
  }
  sk_sp<SkTypeface> onMakeFromFile(const char path[], int index) const override {
    return system_->makeFromFile(path, index);
  }
  sk_sp<SkTypeface> onLegacyMakeTypeface(const char name[],
                                        SkFontStyle style) const override {
    auto family = BundledFamily(name);
    return family ? family->matchStyle(style) : system_->legacyMakeTypeface(name, style);
  }
  sk_sp<SkFontStyleSet> BundledFamily(const char* name) const {
    if (IsGeist(name) && geist_->count()) return geist_;
    if (name && base::EqualsCaseInsensitiveASCII(name, "Geist Mono") &&
        mono_->count()) return mono_;
    return nullptr;
  }
  sk_sp<SkFontMgr> system_;
  sk_sp<SkFontStyleSet> geist_;
  sk_sp<SkFontStyleSet> mono_;
};
}  // namespace

void InitializeBrowserTypography() {
  skia::OverrideDefaultSkFontMgr(sk_make_sp<BrowserFontManager>());
}
}  // namespace frame

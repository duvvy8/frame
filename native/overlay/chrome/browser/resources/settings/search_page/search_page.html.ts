// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from 'chrome://resources/lit/v3_0/lit.rollup.js';

import type {SettingsSearchPageElement} from './search_page.js';

export function getHtml(this: SettingsSearchPageElement) {
  return html`<!--_html_template_start_-->
<settings-section page-title="${this.searchPageTitle_}">
  <div route-path="default">
    <settings-toggle-button
        pref-key="frame.search.empty_remote_suggestions"
        label="Remote suggestions when the search field is empty"
        sub-label="Allow your search provider to show trending or recent searches">
    </settings-toggle-button>
    <settings-toggle-button pref-key="search.suggest_enabled"
        label="Search suggestions as you type"
        sub-label="Send typed search text to your selected search provider">
    </settings-toggle-button>
    <div class="cr-row secondary">
      Quick searches: yt, gh, wiki, r, maps, or !alias followed by your query.
      Edit or remove them in Manage search engines and site search below.
    </div>
    <div class="cr-row">
      <div class="flex secondary">
        Restore built-in quick searches. Your added engines and keyword conflicts are kept.
      </div>
      <cr-button id="restoreFrameQuickSearches"
          @click="${this.onRestoreFrameQuickSearchesClick_}">
        Restore built-ins
      </cr-button>
    </div>
    <!-- Omnibox search engine -->
    <div class="cr-row first">
      <div class="default-search-engine flex">
        ${this.searchPageTitle_}
        <div class="secondary">
          $i18n{searchEngineChoiceEntryPointSubtitle}
        </div>
        <div class="cr-row first">
          <settings-search-engine-icon .engine="${this.defaultSearchEngine_}">
          </settings-search-engine-icon>
          <div class="search-engine-name">${this.defaultSearchEngine_?.name}</div>
          ${this.isDefaultSearchControlledByPolicy_() ? html`
            <cr-policy-pref-indicator
                .pref="${this.defaultSearchProviderDataPref_}">
            </cr-policy-pref-indicator>
          ` : ''}
          <cr-button id="openDialogButton"
              @click="${this.onOpenDialogButtonClick_}"
              ?disabled="${this.isDefaultSearchEngineEnforced_()}">
            $i18n{searchEnginesChange}
          </cr-button>
        </div>
      </div>
      ${this.showSearchEngineListDialog_ ? html`
        <settings-search-engine-list-dialog
            .searchEngines="${this.searchEngines_}"
            @close="${this.onSearchEngineListDialogClose_}"
            @search-engine-changed="${this.onSearchEngineChanged_}">
        </settings-search-engine-list-dialog>
      ` : ''}
      <cr-toast id="confirmationToast" duration="10000">
        <div>${this.confirmationToastLabel_}</div>
      </cr-toast>
    </div>
    ${this.defaultSearchProviderDataPref_?.extensionId ? html`
      ${this.searchSettingsUpdateEnabled_ ? html`
        <div class="cr-row">
          <extension-controlled-message
              class="flex"
              extension-id="${this.defaultSearchProviderDataPref_?.extensionId}"
              extension-name="${
                  this.defaultSearchProviderDataPref_?.controlledByName}"
              ?extension-can-be-disabled="${
                  !!this.defaultSearchProviderDataPref_
                      ?.extensionCanBeDisabled}">
          </extension-controlled-message>
        </div>
      ` : html`
        <div class="cr-row continuation">
          <extension-controlled-indicator
              class="flex"
              extension-id="${this.defaultSearchProviderDataPref_?.extensionId}"
              extension-name="${
                  this.defaultSearchProviderDataPref_?.controlledByName}"
              ?extension-can-be-disabled="${
                  !!this.defaultSearchProviderDataPref_
                      ?.extensionCanBeDisabled}">
          </extension-controlled-indicator>
        </div>
      `}
    ` : ''}

    ${!this.searchSettingsUpdateEnabled_ ? html`
      <!-- Manage search engines -->
      <cr-link-row class="hr" id="enginesSubpageTrigger"
          label="$i18n{searchEnginesManageSiteSearch}"
          @click="${this.onManageSearchEnginesClick_}"
          role-description="$i18n{subpageArrowRoleDescription}"></cr-link-row>
    ` : ''}
  </div>
</settings-section>
<!--_html_template_end_-->`;
}

// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {html} from '//resources/lit/v3_0/lit.rollup.js';
import type {SettingsMemoryPageElement} from './memory_page.js';
import {MemorySaverModeAggressiveness} from './performance_metrics_proxy.js';

export function getHtml(this: SettingsMemoryPageElement) {
  return html`<!--_html_template_start_-->
<settings-section
    ?show-send-feedback-button="${this.showSendFeedbackButton_()}"
    @send-feedback="${this.onSendFeedback_}"
    page-title="$i18n{memoryPageTitle}">
  <settings-toggle-button id="toggleButton" @change="${this.onMemorySaverModeChange_}"
      pref-key="performance_tuning.high_efficiency_mode.state"
      label="$i18n{memorySaverModeLabel}"
      sub-label-with-link="$i18n{memorySaverModeDescription}"
      @sub-label-link-clicked="${this.onMemorySaverSubLabelLinkClicked_}"
      .numericUncheckedValues="${this.numericUncheckedValues_}"
      .numericCheckedValue="${this.numericCheckedValue_}">
  </settings-toggle-button>
  <div class="cr-row">
    <div class="flex">
      <div>Sleep inactive tabs after</div>
      <div class="cr-secondary-text">Requires Memory Saver. Active pages, media,
        edited forms and excluded sites retain native protection. Changing this
        timer restarts the wait for background tabs.</div>
    </div>
    <settings-dropdown-menu id="frameSleepTimeout"
        pref-key="frame.tabs.sleep_timeout_minutes"
        label="Sleep inactive tabs after"
        .menuOptions="${[
          {name: 'Adaptive timing', value: 0},
          {name: '1 minute', value: 1},
          {name: '5 minutes', value: 5},
          {name: '15 minutes', value: 15},
          {name: '30 minutes', value: 30},
          {name: '1 hour', value: 60},
          {name: '2 hours', value: 120},
          {name: '1 day', value: 1440},
        ]}">
    </settings-dropdown-menu>
  </div>
  <cr-collapse id="radioGroupCollapse"
      ?opened="${this.isMemorySaverModeEnabled_()}">
    <div class="cr-row continuation memory-saver-radio-group">
      <settings-radio-group id="radioGroup"
          @change="${this.onMemorySaverModeAggressivenessChange_}"
          pref-key="performance_tuning.high_efficiency_mode.aggressiveness"
          group-aria-label="$i18n{memorySaverModeRadioGroupAriaLabel}">
        <controlled-radio-button id="conservativeButton"
            label="$i18n{memorySaverModeConservativeLabel}"
            name="${MemorySaverModeAggressiveness.CONSERVATIVE}"
            pref-key="performance_tuning.high_efficiency_mode.aggressiveness">
          <div class="cr-secondary-text">
            $i18n{memorySaverModeConservativeDescription}
          </div>
        </controlled-radio-button>
        <controlled-radio-button id="mediumButton"
            label="$i18n{memorySaverModeMediumLabel}"
            name="${MemorySaverModeAggressiveness.MEDIUM}"
            pref-key="performance_tuning.high_efficiency_mode.aggressiveness">
          <div class="cr-secondary-text">
            $i18n{memorySaverModeMediumDescription}
          </div>
        </controlled-radio-button>
        <controlled-radio-button id="aggressiveButton"
            label="$i18n{memorySaverModeAggressiveLabel}"
            name="${MemorySaverModeAggressiveness.AGGRESSIVE}"
            pref-key="performance_tuning.high_efficiency_mode.aggressiveness">
          <div class="cr-secondary-text">
            $i18n{memorySaverModeAggressiveDescription}
          </div>
        </controlled-radio-button>
      </settings-radio-group>
    </div>
  </cr-collapse>
</settings-section><!--_html_template_end_-->`;
}

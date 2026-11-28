/**
 * @file src/ui/shell_layout.cpp
 * @brief Implements the television-safe shell and scrolling card grid geometry.
 */
// class header include
#include "src/ui/shell_layout.h"

// standard includes
#include <algorithm>
#include <cmath>

namespace {
  /**
   * @brief Keep desktop side margins within a useful range across window widths.
   * @param width Desktop viewport width in layout units.
   * @return Left and right margin.
   */
  int desktop_side_margin(int width) {
    return std::clamp(width / 20, 18, 64);
  }
}  // namespace

namespace ui {

  ShellRect framebuffer_repaint_rect(ShellRect logical, int logicalWidth, int framebufferWidth, int framebufferHeight) {
    if (logical.w <= 0 || logical.h <= 0 || logicalWidth <= 0 || framebufferWidth <= 0 || framebufferHeight <= 0) {
      return {};
    }
    const auto left = std::clamp<std::int64_t>(static_cast<std::int64_t>(logical.x) * framebufferWidth / logicalWidth - 2, 0, framebufferWidth);
    const auto right = std::clamp<std::int64_t>(((static_cast<std::int64_t>(logical.x) + logical.w) * framebufferWidth + logicalWidth - 1) / logicalWidth + 2, 0, framebufferWidth);
    const auto top = std::clamp<std::int64_t>(static_cast<std::int64_t>(logical.y) - 1, 0, framebufferHeight);
    const auto bottom = std::clamp<std::int64_t>(static_cast<std::int64_t>(logical.y) + logical.h + 1, 0, framebufferHeight);
    return right > left && bottom > top ? ShellRect {static_cast<int>(left), static_cast<int>(top), static_cast<int>(right - left), static_cast<int>(bottom - top)} : ShellRect {};
  }

  std::size_t stable_list_first_row(std::size_t count, std::size_t selected, std::size_t visible, std::size_t previousFirst) {
    if (count == 0 || visible == 0 || count <= visible) {
      return 0;
    }
    selected = std::min(selected, count - 1);
    const auto first = std::min(previousFirst, count - visible);
    if (selected < first) {
      return selected;
    }
    return selected - first >= visible ? selected - visible + 1 : first;
  }

  ShellLayout calculate_shell_layout(int width, int height, int hintGlyphSize, int hintRowsHeight, ShellLayoutOptions options) {
    constexpr int dividerOffset = 6;
    const int spacingHeight = options.desktop ? std::min(height, 720) : height;
    const int marginX = options.desktop ? desktop_side_margin(width) : width / 20;
    const int marginY = spacingHeight / 20;
    const int gap = std::max(10, spacingHeight / 40);
    const int navigationHeight = std::max(38, spacingHeight / 12);
    const int headingHeight = std::max(60, spacingHeight / 8);
    const int innerWidth = std::max(1, width - marginX * 2);
    const ShellRect navigation {marginX, marginY, innerWidth, navigationHeight};
    const bool stackToolbar = options.desktop && options.hasToolbar && width < 740;
    const ShellRect toolbar = stackToolbar ? ShellRect {marginX, marginY + navigationHeight + gap, innerWidth, 48} : navigation;
    const int saveSize = std::clamp(hintGlyphSize + 6, 1, innerWidth);
    const int hintsHeight = std::max(hintGlyphSize, hintRowsHeight);
    const int controlsHeight = std::max(saveSize, hintsHeight);
    const int headingY = toolbar.y + toolbar.h + gap;
    const int contentTop = headingY + headingHeight + gap;
    // Place the divider above the overscan-safe area and center the controls in all rows below it.
    const int footerSafeTop = height - std::max(8, spacingHeight / 40) - controlsHeight;
    const int footerTop = footerSafeTop - dividerOffset + 1;
    const int footerHeight = height - footerTop;
    return {
      navigation,
      toolbar,
      {marginX, headingY, innerWidth, headingHeight},
      {marginX, contentTop, innerWidth, std::max(1, footerSafeTop - gap - contentTop)},
      {marginX, footerTop, innerWidth, footerHeight},
      {marginX, footerTop + (footerHeight - hintsHeight) / 2, std::max(0, innerWidth - saveSize - gap), hintsHeight},
      {marginX + innerWidth - saveSize, footerTop + (footerHeight - saveSize) / 2, saveSize, saveSize},
      gap,
    };
  }

  /**
   * @brief Fit a positive window into the desktop layout's minimum logical dimensions.
   */
  ShellRect calculate_desktop_shell_viewport(int width, int height, float displayScale) {
    if (width <= 0 || height <= 0) {
      return {};
    }
    const float scale = std::min({displayScale > 0.0f ? displayScale : 1.0f, static_cast<float>(width) / 440.0f, static_cast<float>(height) / 480.0f});
    const float logicalWidth = static_cast<float>(width) / scale;
    const float logicalHeight = static_cast<float>(height) / scale;
#ifdef NXDK
    // The guest tests exercise this shared layout code. nxdk's lroundf is a stub.
    return {0, 0, static_cast<int>(static_cast<double>(logicalWidth) + 0.5), static_cast<int>(static_cast<double>(logicalHeight) + 0.5)};
#else
    return {0, 0, static_cast<int>(std::lround(logicalWidth)), static_cast<int>(std::lround(logicalHeight))};
#endif
  }

  DesktopGridColumns calculate_desktop_grid_columns(int width) {
    const int contentWidth = width - 2 * desktop_side_margin(width);
    return {static_cast<std::size_t>(std::max(1, contentWidth / 300)), static_cast<std::size_t>(std::max(1, contentWidth / 245))};
  }

  ShellMenuPanels calculate_desktop_menu_panels(const ShellLayout &layout, bool settings) {
    const auto &[x, y, width, height] = layout.content;
    if (const int viewportHeight = layout.footer.y + layout.footer.h; height >= 410 && (width < 680 || width < viewportHeight)) {
      const int menuHeight = std::min(260, (height - layout.gap) * 2 / 5);
      return {{x, y, width, menuHeight}, {x, y + menuHeight + layout.gap, width, height - menuHeight - layout.gap}};
    }

    const int menuWidth = std::max(154, width * 30 / 100);
    const int detailX = x + menuWidth + layout.gap;
    const int detailY = settings ? layout.navigation.y : y;
    return {{x, y, menuWidth, height}, {detailX, detailY, x + width - detailX, y + height - detailY}};
  }

  ShellRect calculate_settings_panel(const ShellLayout &layout) {
    const int x = layout.content.x + layout.content.w * 30 / 100 + layout.gap;
    return {x, layout.navigation.y, layout.content.x + layout.content.w - x, layout.content.y + layout.content.h - layout.navigation.y};
  }

  ShellSettingsDetailLayout calculate_settings_detail_layout(ShellRect bounds, int lineHeight) {
    if (bounds.w <= 0 || bounds.h <= 0 || lineHeight <= 0) {
      return {};
    }
    const int helpHeight = std::min(bounds.h / 2, lineHeight * 5 + 6);
    const int titleHeight = std::min(lineHeight, helpHeight);
    const int descriptionY = bounds.y + std::min(helpHeight, titleHeight + 6);
    const int headerY = bounds.y + std::min(bounds.h, helpHeight + 20);
    const int headerHeight = std::min(lineHeight + 8, bounds.y + bounds.h - headerY);
    const int optionsY = headerY + headerHeight;
    return {
      {bounds.x, bounds.y, bounds.w, titleHeight},
      {bounds.x, descriptionY, bounds.w, bounds.y + helpHeight - descriptionY},
      {bounds.x, headerY, bounds.w, headerHeight},
      {bounds.x, optionsY, bounds.w, bounds.y + bounds.h - optionsY},
    };
  }

  std::vector<ShellRect> calculate_keypad_columns(ShellRect bounds, std::size_t columnCount, int preferredWidth, int gap) {
    if (bounds.w <= 0 || bounds.h <= 0 || columnCount == 0U || columnCount > static_cast<std::size_t>(bounds.w)) {
      return {};
    }
    const auto columns = static_cast<int>(columnCount);
    const int spacing = columns == 1 ? 0 : std::clamp(gap, 0, (bounds.w - columns) / (columns - 1));
    const int columnWidth = std::clamp(preferredWidth, 1, (bounds.w - spacing * (columns - 1)) / columns);
    const int groupWidth = columnWidth * columns + spacing * (columns - 1);
    const int startX = bounds.x + (bounds.w - groupWidth) / 2;
    std::vector<ShellRect> result;
    for (int column = 0; column < columns; ++column) {
      result.push_back({startX + column * (columnWidth + spacing), bounds.y, columnWidth, bounds.h});
    }
    return result;
  }

  ShellRect calculate_shell_dialog(int width, int height, int desiredWidth, int desiredHeight) {
    const int w = std::clamp(desiredWidth, 1, std::max(1, width - width / 10));
    const int h = std::clamp(desiredHeight, 1, std::max(1, height - height / 10));
    return {(width - w) / 2, (height - h) / 2, w, h};
  }

  StreamStartupLayout calculate_stream_startup_layout(int width, int height, int titleLineHeight, int bodyLineHeight) {
    if (width <= 0 || height <= 0 || titleLineHeight <= 0 || bodyLineHeight <= 0) {
      return {};
    }

    const int padding = std::max(20, height / 40);
    constexpr int titleTop = 28;
    constexpr int gap = 12;
    constexpr int progressHeight = 8;
    const int hintHeight = 2 * bodyLineHeight;
    const int fixedHeight = titleTop + titleLineHeight + 3 * gap + progressHeight + hintHeight + padding;
    const auto card = calculate_shell_dialog(width, height, std::min(760, width * 3 / 4), fixedHeight + 3 * bodyLineHeight);
    const int innerWidth = card.w - 2 * padding;
    const int bodyHeight = card.h - fixedHeight;
    if (innerWidth <= 0 || bodyHeight < bodyLineHeight) {
      return {};
    }

    const int left = card.x + padding;
    const int bodyY = card.y + titleTop + titleLineHeight + gap;
    const int progressY = bodyY + bodyHeight + gap;
    return {
      card,
      {left, card.y + titleTop, innerWidth, titleLineHeight},
      {left, bodyY, innerWidth, bodyHeight},
      {left, progressY, innerWidth, progressHeight},
      {left, progressY + progressHeight + gap, innerWidth, hintHeight},
    };
  }

  double shell_spinner_angle(std::uint32_t elapsedMilliseconds) {
    constexpr std::uint32_t periodMilliseconds = 1200U;
    return static_cast<double>(elapsedMilliseconds % periodMilliseconds) * 360.0 / periodMilliseconds;
  }

  std::vector<ShellRect> calculate_notification_stack(ShellRect area, const std::vector<int> &newestFirstHeights) {
    if (area.w <= 0 || area.h <= 0) {
      return {};
    }
    std::vector<ShellRect> cards;
    int bottom = area.y + area.h;
    for (const int desiredHeight : newestFirstHeights) {
      const int height = std::clamp(desiredHeight, 1, area.h);
      if (cards.size() == MAX_VISIBLE_NOTIFICATIONS || height > bottom - area.y) {
        break;
      }
      cards.push_back({area.x, bottom - height, area.w, height});
      bottom -= height + 10;
    }
    return cards;
  }

  ShellGridLayout calculate_shell_grid(ShellRect bounds, std::size_t itemCount, std::size_t columns, std::size_t selectedIndex, int preferredHeight, int gap) {
    if (bounds.w <= 0 || bounds.h <= 0 || itemCount == 0U || columns == 0U) {
      return {};
    }
    ShellGridLayout grid {};
    grid.columns = static_cast<int>(columns);
    grid.gap = std::max(0, gap);
    grid.totalRows = static_cast<int>((itemCount + columns - 1U) / columns);
    grid.visibleRows = std::min(grid.totalRows, std::max(1, (bounds.h + grid.gap) / (std::max(1, preferredHeight) + grid.gap)));
    const auto selectedRow = static_cast<int>(std::min(selectedIndex, itemCount - 1U) / columns);
    grid.startRow = std::clamp(selectedRow + 1 - grid.visibleRows, 0, grid.totalRows - grid.visibleRows);
    grid.scrollbarWidth = grid.totalRows > grid.visibleRows ? 4 : 0;
    const int scrollbarSpace = grid.scrollbarWidth > 0 ? grid.scrollbarWidth + grid.gap : 0;
    grid.cellWidth = std::max(1, (bounds.w - scrollbarSpace - grid.gap * (grid.columns - 1)) / grid.columns);
    grid.cellHeight = std::max(1, std::min(std::max(1, preferredHeight), (bounds.h - grid.gap * (grid.visibleRows - 1)) / grid.visibleRows));
    return grid;
  }

  ShellRect shell_grid_cell(ShellRect bounds, const ShellGridLayout &grid, std::size_t index) {
    if (grid.columns <= 0 || grid.visibleRows <= 0) {
      return {};
    }
    const auto row = static_cast<int>(index / static_cast<std::size_t>(grid.columns));
    if (row < grid.startRow || row >= grid.startRow + grid.visibleRows || row >= grid.totalRows) {
      return {};
    }
    return {
      bounds.x + static_cast<int>(index % static_cast<std::size_t>(grid.columns)) * (grid.cellWidth + grid.gap),
      bounds.y + (row - grid.startRow) * (grid.cellHeight + grid.gap),
      grid.cellWidth,
      grid.cellHeight,
    };
  }

}  // namespace ui

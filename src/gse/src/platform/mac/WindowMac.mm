#include "gse/Platform.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace nascent {
    static constexpr NSApplicationPresentationOptions SWITCH_BLOCKING_OPTIONS = NSApplicationPresentationDisableProcessSwitching | NSApplicationPresentationDisableForceQuit | NSApplicationPresentationDisableSessionTermination | NSApplicationPresentationDisableHideApplication;
    static constexpr NSWindowStyleMask WINDOW_STYLE = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;

    static std::unordered_map<NSInteger, NSWindowLevel> raised_levels;
    static bool switching_observed = false;

    bool fit_window_to_screen(int32_t& width, int32_t& height) {
        @autoreleasepool {
            NSScreen* screen = NSScreen.screens.firstObject;

            if (screen == nil || width <= 0 || height <= 0) {
                return false;
            }

            NSRect area = screen.visibleFrame;
            NSRect content = NSMakeRect(0, 0, 100, 100);
            NSRect frame = [NSWindow frameRectForContentRect:content styleMask:WINDOW_STYLE];
            double usable_width = area.size.width - (frame.size.width - content.size.width);
            double usable_height = area.size.height - (frame.size.height - content.size.height);

            if (usable_width <= 0 || usable_height <= 0) {
                return false;
            }

            double scale = std::min({1.0, usable_width / width, usable_height / height});

            if (scale >= 1.0) {
                return false;
            }

            width = (int32_t)std::floor(width * scale);
            height = (int32_t)std::floor(height * scale);
            return true;
        }
    }

    static void unblock_app_switching() {
        NSApplicationPresentationOptions options = NSApp.presentationOptions;

        if ((options & SWITCH_BLOCKING_OPTIONS) == 0) {
            return;
        }

        @try {
            NSApp.presentationOptions = options & ~SWITCH_BLOCKING_OPTIONS;
        }
        @catch (NSException*) {
            return;
        }
    }

    static void lower_raised_windows() {
        for (NSWindow* window in NSApp.windows) {
            if (window.level > NSNormalWindowLevel) {
                raised_levels[window.windowNumber] = window.level;
                window.level = NSNormalWindowLevel;
            }
        }
    }

    static void restore_raised_windows() {
        for (NSWindow* window in NSApp.windows) {
            auto found = raised_levels.find(window.windowNumber);

            if (found != raised_levels.end()) {
                window.level = found->second;
            }
        }

        raised_levels.clear();
        unblock_app_switching();
    }

    std::optional<std::filesystem::path> choose_folder(const std::string& title) {
        __block std::optional<std::filesystem::path> chosen;
        NSString* message = [NSString stringWithUTF8String:title.c_str()];

        void (^show)(void) = ^{
          @autoreleasepool {
              NSOpenPanel* panel = [NSOpenPanel openPanel];
              panel.canChooseDirectories = YES;
              panel.canChooseFiles = NO;
              panel.allowsMultipleSelection = NO;
              panel.message = message;
              NSWindow* window = NSApp.mainWindow != nil ? NSApp.mainWindow : NSApp.keyWindow;

              if (window != nil) {
                  panel.level = std::max(panel.level, window.level + 1);
              }

              if ([panel runModal] == NSModalResponseOK && panel.URL != nil) {
                  chosen = std::filesystem::path(panel.URL.path.UTF8String);
              }
              else {
                  chosen = std::filesystem::path();
              }
          }
        };

        if (NSThread.isMainThread) {
            show();
        }
        else {
            dispatch_sync(dispatch_get_main_queue(), show);
        }

        return chosen;
    }

    bool alt_held() {
        return (NSEvent.modifierFlags & NSEventModifierFlagOption) != 0;
    }

    std::optional<bool> application_focused() {
        return NSApp != nil ? std::optional<bool>(NSApp.isActive) : std::nullopt;
    }

    void allow_app_switching_in_fullscreen() {
        if (switching_observed || NSApp == nil) {
            return;
        }

        switching_observed = true;
        NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
        [center addObserverForName:NSApplicationWillResignActiveNotification
                            object:nil
                             queue:nil
                        usingBlock:^(NSNotification*) {
                          lower_raised_windows();
                        }];
        [center addObserverForName:NSApplicationDidBecomeActiveNotification
                            object:nil
                             queue:nil
                        usingBlock:^(NSNotification*) {
                          restore_raised_windows();
                        }];
        [center addObserverForName:NSWindowDidBecomeKeyNotification
                            object:nil
                             queue:nil
                        usingBlock:^(NSNotification*) {
                          unblock_app_switching();
                        }];
        unblock_app_switching();
    }
}

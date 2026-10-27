//
//  ScreensaverHelper.swift
//  infinidream
//
//  Objective-C bridge for PaperSaver screensaver + wallpaper extension APIs.
//

import Foundation
import PaperSaverKit

@objc public class ScreensaverHelper: NSObject {
    /// PaperSaver 0.4+ validates wallpaper extensions against the
    /// `com.apple.wallpaper` pluginkit point. Our appex is still
    /// `com.apple.screensaver` (Phase 1); disable that check so the Desktop
    /// slot can be written with our bundle ID. WallpaperAgent already hosts
    /// screensaver appexes as desktop-level content.
    private lazy var paperSaver: PaperSaver = {
        var config = PaperSaverConfiguration()
        config.validatesWallpaperExtensions = false
        return PaperSaver(configuration: config)
    }()

    @objc nonisolated(unsafe) public static let shared = ScreensaverHelper()
    
    private override init() {
        super.init()
    }
    
    /// PaperSaver identifies an appex screensaver by the .appex file stem
    /// (PRODUCT_NAME), not CFBundleDisplayName.
    private var moduleName: String {
        let bid = Bundle.main.bundleIdentifier ?? ""
        return bid.contains("stage") ? "infinidream-stage" : "infinidream"
    }

    /// Bundle ID of the embedded screensaver appex (must match PRODUCT_BUNDLE_IDENTIFIER).
    private var extensionBundleID: String {
        let bid = Bundle.main.bundleIdentifier ?? ""
        return bid.contains("stage")
            ? "ai.infinidream.stage.app.screensaver"
            : "ai.infinidream.alpha.app.screensaver"
    }

    // MARK: - Screensaver (Idle slot)

    @objc public func isInfinidreamActive() -> Bool {
        let active = paperSaver.getActiveScreensavers()
        return active.contains(moduleName) || active.contains(extensionBundleID)
    }
    
    /// Set infinidream as the active screensaver
    @objc public func setInfinidreamAsActive(completion: @escaping (Bool, Error?) -> Void) {
        let module = moduleName
        Task {
            do {
                try await paperSaver.setScreensaverEverywhere(module: module)
                await MainActor.run {
                    completion(true, nil)
                }
            } catch {
                await MainActor.run {
                    completion(false, error)
                }
            }
        }
    }
    
    /// Check if any screensaver (not just infinidream) is active
    @objc public func hasActiveScreensaver() -> Bool {
        return !paperSaver.getActiveScreensavers().isEmpty
    }

    /// Get the name of the first currently active screensaver
    @objc public func getActiveScreensaverName() -> String? {
        return paperSaver.getActiveScreensavers().first
    }

    // MARK: - Wallpaper (Desktop slot)

    /// True when our appex is the System Settings desktop wallpaper extension.
    @objc public func isInfinidreamWallpaperActive() -> Bool {
        guard #available(macOS 14.0, *) else { return false }
        return paperSaver.getCurrentWallpaperExtension() == extensionBundleID
    }

    /// Set our appex as the live desktop wallpaper on every display/Space.
    @objc public func setInfinidreamAsWallpaper(completion: @escaping (Bool, Error?) -> Void) {
        let bundleID = extensionBundleID
        Task {
            do {
                if #available(macOS 14.0, *) {
                    _ = try await paperSaver.setWallpaperExtension(
                        bundleID: bundleID,
                        slot: .desktop
                    )
                    await MainActor.run { completion(true, nil) }
                } else {
                    await MainActor.run {
                        completion(
                            false,
                            NSError(
                                domain: "ScreensaverHelper",
                                code: 1,
                                userInfo: [
                                    NSLocalizedDescriptionKey:
                                        "Live wallpaper requires macOS 14 or later"
                                ]
                            )
                        )
                    }
                }
            } catch {
                await MainActor.run { completion(false, error) }
            }
        }
    }

    /// Set our appex as both desktop wallpaper and screensaver (one store write).
    @objc public func setInfinidreamAsWallpaperAndScreensaver(
        completion: @escaping (Bool, Error?) -> Void
    ) {
        let bundleID = extensionBundleID
        Task {
            do {
                if #available(macOS 14.0, *) {
                    _ = try await paperSaver.setWallpaperExtension(
                        bundleID: bundleID,
                        slots: [.desktop, .screensaver]
                    )
                    await MainActor.run { completion(true, nil) }
                } else {
                    // Fall back to screensaver-only on older macOS.
                    try await paperSaver.setScreensaverEverywhere(module: moduleName)
                    await MainActor.run { completion(true, nil) }
                }
            } catch {
                await MainActor.run { completion(false, error) }
            }
        }
    }
}
